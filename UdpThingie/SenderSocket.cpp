#include "pch.h"
#include "SenderSocket.h"
#include "PacketTypes.h"

#include <cassert>
#include <string>
#include <sstream>
#include <unordered_map>
#include <chrono>
#include <iostream>
#include <iomanip>

using namespace __fucker;

///////////////////////////////////////////////////////////////////////////////////////////////////
SenderSocket::SenderSocket()
{
    this->setUpStatsThread();
    this->constructTime = std::chrono::steady_clock::now();
}

/**
 * Clean up any dangling resources.
 */
SenderSocket::~SenderSocket() noexcept(false)
{
    // tell the stats thread to go commit sudoku
    if (!SetEvent(this->quitEvent)) {
        std::string msg = "SetEvent(): " + GetLastError();
        throw std::runtime_error(msg);
    }

    // close socket if still open
    if (this->sock != INVALID_SOCKET) {
        closesocket(this->sock);
        this->sock = INVALID_SOCKET;
    }

    // wait for the stats thread to actually die
    WaitForSingleObject(this->statsThread, 500);
    CloseHandle(this->statsThread);
    this->statsThread = INVALID_HANDLE_VALUE;

    CloseHandle(this->quitEvent);
    this->quitEvent = INVALID_HANDLE_VALUE;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * @brief Attempts to open a connection to the remote host.
 */
void SenderSocket::open(const std::string& host, uint16_t port, size_t window, float rtt, float speed, float loss[2])
{
    // sanity checking
    if (this->isConnected) {
        throw SocketError(SocketError::kStatusConnected);
    }

    // resolve address and establish the socket
    struct sockaddr_storage storage;
    memset(&storage, 0, sizeof(struct sockaddr_storage));

    SenderSocket::resolve(host, &storage);

    struct sockaddr_in* addr = (struct sockaddr_in*) &storage;
    addr->sin_port = htons(port);

    this->setUpSocket(&storage);
    this->hostStr = host;

    // build SYN packet
    SenderSynPacket syn;
    memset(&syn, 0, sizeof(SenderSynPacket));

    syn.header.flags.magic = kFlagsMagic;
    syn.header.flags.syn = 1;

    syn.lp.bufferSize = (DWORD) (window + kMaxRetransmissions); // W+R
    syn.lp.RTT = rtt;
    syn.lp.speed = speed;
    syn.lp.pLoss[0] = loss[0];
    syn.lp.pLoss[1] = loss[1];

    // prepare internal state, then send the SYN packet
    this->currentSeq = 0;
    this->startTime = std::chrono::steady_clock::now();
    this->rtoDelay = kRetransmissionTimeout;

    this->sendPacketRetransmit(&syn, sizeof(SenderSynPacket), kMaxRetransmissionsSYN, true, false, "SYN");

    // if we get here, the connection was successful
    this->synAckTime = std::chrono::steady_clock::now();
    this->isConnected = true;

    // begin executing the stats thread
    ResumeThread(this->statsThread);
}

/**
 * @brief Closes the connection. A FIN packet is sent before the socket is closed.
*/
void SenderSocket::close()
{
    if (!this->isConnected) {
        throw SocketError(SocketError::kStatusNotConnected);
    }

    // build FIN packet
    SenderPacketHeader fin;
    memset(&fin, 0, sizeof(SenderPacketHeader));

    fin.flags.magic = kFlagsMagic;
    fin.flags.fin = 1;

    // send it
    this->sendPacketRetransmit(&fin, sizeof(SenderPacketHeader), kMaxRetransmissions, false, true, "FIN");

    // actually close the socket
    if (this->sock != INVALID_SOCKET) {
        closesocket(this->sock);
        this->sock = INVALID_SOCKET;
    }

    this->isConnected = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Sends data from the given buffer.
 */
void SenderSocket::send(void* data, size_t length)
{
    if (!this->isConnected) {
        throw SocketError(SocketError::kStatusNotConnected);
    }

    // TODO: implement
}

/**
 * @brief Sets up the UDP socket and connects it.
*/
void SenderSocket::setUpSocket(struct sockaddr_storage *addr)
{
    int err;

    // create the socket
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1) {
        throw SocketError(SocketError::kStatusSystemError, WSAGetLastError());
    }

    // bind it to any local address
    struct sockaddr_in local = { 0 };
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = htons(0);

    err = bind(sock, (struct sockaddr*)&local, sizeof(local));
    if (err == -1) {
        throw SocketError(SocketError::kStatusSystemError, WSAGetLastError());
    }

    // increase the receive/transmit buffer sizes
    const int kRxBufSz = (20 * 1000 * 1000), kTxBufSz = (20 * 1000 * 1000);

    err = setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (const char *) &kRxBufSz, sizeof(int));
    if (err == SOCKET_ERROR) {
        throw SocketError(SocketError::kStatusSystemError, WSAGetLastError());
    }

    err = setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (const char*) &kTxBufSz, sizeof(int));
    if (err == SOCKET_ERROR) {
        throw SocketError(SocketError::kStatusSystemError, WSAGetLastError());
    }

    // socket is good
    this->sock = sock;
    this->host = *addr;
}

/**
 * @brief Implements the raw transmit logic that will retransmit a packet a given number of times
 * before giving up.
 * 
 * @param data Packet to send (including header)
 * @param length Length of packet, in bytes
 * @param attempts Number of times to re-try transmitting the packet
 * @param updateRto If the round-trip delay should be updated
 * @param log If true, some detailed info is logged to the console
*/
void SenderSocket::sendPacketRetransmit(void* data, size_t length, size_t attempts, bool updateRto, bool log, const std::string &kind)
{
    int err;
    double rto = 0;

    // fixed timeout value (for retransmission)
    struct timeval timeout = { 0 };
    timeout.tv_sec = (int) this->rtoDelay;
    timeout.tv_usec = (int) ((this->rtoDelay - ((int64_t)this->rtoDelay)) * 1000 * 1000);

//    std::cout << "sec " << timeout.tv_sec << " usec " << timeout.tv_usec << "; " << this->rtoDelay << std::endl;

    // receive buffer for ack (and where it came from)
    static const size_t kReceiveSz = kMaxPacketSize;
    char receive[kReceiveSz] = { 0 };
    ReceiverPacketHeader* rxHdr = reinterpret_cast<ReceiverPacketHeader*>(receive);

    struct sockaddr_storage respFrom;
    socklen_t respFromLen = sizeof(struct sockaddr_storage);

    // fill in outgoing sequence number
    SenderPacketHeader* hdr = reinterpret_cast<SenderPacketHeader*>(data);
    hdr->seq = this->currentSeq;

    // retransmit the max amount allowed
    for (size_t i = 0; i < attempts; i++) {
        auto sentAt = std::chrono::steady_clock::now();

        // send the packet
        err = sendto(this->sock, (const char*)data, (int)length, 0, (struct sockaddr*)&this->host,
            sizeof(struct sockaddr_in));
        if (err == -1) {
            throw SocketError(SocketError::kStatusSendFailed, WSAGetLastError());
        }

        if (log && this->debug) {
            auto nowTs = std::chrono::steady_clock::now();
            double now = std::chrono::duration_cast<std::chrono::milliseconds>(nowTs - this->startTime).count() / 1000.f;

            std::cout << "[" << std::fixed << std::setprecision(3) << std::setw(6) << now << "] --> " 
                      << kind << ' ' << hdr->seq << " (attempt " << (i + 1) << " of " 
                      << attempts << ", RTO " << this->rtoDelay << ")" << " to " 
                      << this->hostStr << std::endl;
        }

        // wait for activity on the socket
        fd_set set;
        FD_ZERO(&set);
        FD_SET(this->sock, &set);

        err = select(0, &set, nullptr, nullptr, &timeout);
        if (err == -1) {
            throw SocketError(SocketError::kStatusSystemError, WSAGetLastError());
        }
        // handle timeouts
        if (err == 0) {
            continue;
        }

        // receive a response
        err = recvfrom(this->sock, receive, kReceiveSz, 0, (struct sockaddr*)&respFrom, &respFromLen);
        if (err == -1) {
            throw SocketError(SocketError::kStatusRecvFailed, WSAGetLastError());
        }
        assert(err >= sizeof(ReceiverPacketHeader));

        // calculate round-trip time for this packet
        auto receivedAt = std::chrono::steady_clock::now();
        rto = (std::chrono::duration_cast<std::chrono::milliseconds>(receivedAt - sentAt).count() / 1000.f);

        goto success;
    }

    // if we get here, max number of retransmissions exhausted
    throw SocketError(SocketError::kStatusTimeout);
    
success:;
    // on successful transmission, increment the sequence counter and update delay
//    this->currentSeq++;

    // print how long this song and dance took
    auto nowTs = std::chrono::steady_clock::now();
    double now = std::chrono::duration_cast<std::chrono::milliseconds>(nowTs - this->startTime).count() / 1000.f;

    if (log) {
        std::cout << "[" << std::fixed << std::setprecision(3) << std::setw(6) << now << "] <-- "
                  << kind << ((rxHdr->flags.ack) ? "-ACK " : " ") << rxHdr->ackSeq << " window "
                  << std::hex << std::setw(8) << std::setfill('0') << rxHdr->receiveWindow;
    }
    if (updateRto) {
        rto *= 3.f;

        if (log) {
            std::cout << "; setting initial RTO to " << rto;
        }
        this->rtoDelay = rto;
    }
    if (log) {
        std::cout << std::endl;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Attempts to resolve the given hostname.
 */
void SenderSocket::resolve(const std::string& host, struct sockaddr_storage* outAddr)
{
    int err;

    // common setup of the output address
    sockaddr_in* addrV4 = reinterpret_cast<sockaddr_in*>(outAddr);
    outAddr->ss_family = AF_INET;

    // try to parse as IP string
    IN_ADDR resolved = { 0 };
    err = inet_pton(AF_INET, host.c_str(), &resolved);

    if (err == 1) {
        addrV4->sin_addr = resolved;
        return;
    } else if (err == -1) {
        throw SocketError(SocketError::kStatusInvalidHost, WSAGetLastError());
    }

    // hints for lookup
    struct addrinfo hints = { 0 };
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags |= AI_CANONNAME;

    // otherwise, we need to perform a DNS lookup
    struct addrinfo* result = nullptr;
    struct addrinfo* ptr = nullptr;

    err = getaddrinfo(host.c_str(), nullptr, &hints, &result);
    if (err != 0) {
        throw SocketError(SocketError ::kStatusInvalidHost, "getaddrinfo(): " + std::to_string(err));
    }

    // find the first address that's IPv4
    for (ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
        char name[64] = { 0 };

        if (ptr->ai_family == AF_INET) {
            memcpy(outAddr, ptr->ai_addr, ptr->ai_addrlen);
            goto beach;
        }
    }

    // no suitable addresses
    throw SocketError(SocketError::kStatusInvalidHost, "no addresses for host");

    // cleanup
beach:;
    freeaddrinfo(result);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * @brief Trampoline to jump into the class main method
 * @param ctx Context passed to thread creation
*/
DWORD WINAPI __fucker::StatsThreadEntry(LPVOID ctx)
{
    static_cast<SenderSocket*>(ctx)->statsThreadMain();
    return 0;
}

/**
 * @brief Initializes the stats thread.
*/
void SenderSocket::setUpStatsThread()
{
    using namespace __fucker;

    // set up the quit event
    this->quitEvent = CreateEvent(nullptr, false, false, L"StatsThreadQuit");
    if (this->quitEvent == (HANDLE)ERROR_INVALID_HANDLE) {
        std::string msg = "CreateEvent(): " + GetLastError();
        throw std::runtime_error(msg);
    }

    // set up thread
    this->statsThread = CreateThread(nullptr, kStackSize, StatsThreadEntry, this, CREATE_SUSPENDED, nullptr);
    assert(this->statsThread);

    SetThreadPriority(this->statsThread, THREAD_PRIORITY_ABOVE_NORMAL);
}

/**
 * @brief Main loop for the stats thread
*/
void SenderSocket::statsThreadMain()
{
    using namespace std::chrono;
    // wait on the quit event
    while (WaitForSingleObject(this->quitEvent, 2000) == WAIT_TIMEOUT) {
        // lol
        this->statsThreadPrint(std::cout);
    }

    // clean up
    std::cout << std::endl << std::flush;
}

/**
 * @brief Prints a single stats line to the given stream.
 * @param out Stream to receive stats text
*/
void SenderSocket::statsThreadPrint(std::ostream& out, bool newline)
{
    // seconds since constructyboi
    auto now = std::chrono::steady_clock::now();
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(now - this->constructTime).count();

    out << "[" << std::setw(2) << secs << "] ";

    // sender base and mbytes acked
    double mbAcked = 0;
    out << "B " << std::setw(6) << this->stats.senderBase << " (" << std::setw(6) << mbAcked
        << " MB) ";

    // next sequence number
    out << "N " << std::setw(6) << this->stats.nextSeq << ' ';

    // timeout/fast retransmit/effective window size
    out << "T " << std::setw(2) << this->stats.timeout << ' '
        << "F " << std::setw(2) << this->stats.fastReTx << ' '
        << "W " << std::setw(2) << this->stats.effectiveWindow << ' ';

    // goodput and RTT
    double goodput = ((double)this->stats.goodput) / 1000.f / 1000.f;
    double rtt = ((double)this->stats.estimatedRtt) / 1000.f;

    out << "S " << std::setw(6) << goodput << " Mbps "
        << " RTT " << std::setw(5) << std::fixed << std::setprecision(3) << rtt;

    // lastly, newline if requested
    if (newline) {
        out << std::endl;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
/// Default descriptive texts for each of the error codes
const std::unordered_map<SenderSocket::SocketError::Type, std::string> SenderSocket::SocketError::kDefaultMessages = {
    {kStatusOk, "No error (yo wtf)"},
    { kStatusConnected, "Socket already connected" },
    { kStatusNotConnected, "Cannot perform IO on disconnected socket" },
    { kStatusInvalidHost, "Destination host is invalid" },
    { kStatusSendFailed, "send() failed" },
    { kStatusRecvFailed, "recvfrom() failed" },
    { kStatusTimeout, "Timeout after exhausting retransmissions" },
};

/**
 * @brief Creates a new error with no additional information beyond a type.
*/
SenderSocket::SocketError::SocketError(Type t)
    : type(t)
{
    try {
        this->message = kDefaultMessages.at(t);
    } catch (std::out_of_range&) {
        this->message = "unknown error";
    }
}

/**
 * @brief Creates a new error with detailed error string.
*/
SenderSocket::SocketError::SocketError(Type t, const std::string &detail)
    : type(t)
{
    std::stringstream str;

    try {
        str << kDefaultMessages.at(t);
    } catch (std::out_of_range&) {
        str << "unknown error";
    }

    str << ": " << detail;
    
    this->message = str.str();
}