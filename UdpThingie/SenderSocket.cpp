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
#include <algorithm>

using namespace __fucker;

///////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * @brief Constructs the sender socket.
 * 
 * All secondary threads (stats and worker) are created here, so that if thread creation fails, it
 * will fail early. That makes error handling simpler. These threads are started in a suspended
 * state such that only after the connection is opened will they be started.
 */
SenderSocket::SenderSocket() noexcept(false)
{
    // create various events
    this->quitEvent = CreateEvent(nullptr, false, false, L"StatsThreadQuit");
    if (this->quitEvent == (HANDLE)ERROR_INVALID_HANDLE) {
        std::string msg = "CreateEvent(): " + GetLastError();
        throw std::runtime_error(msg);
    }

    this->abortEvent = CreateEvent(nullptr, false, false, L"ConnectionAbort");
    if (this->abortEvent == (HANDLE)ERROR_INVALID_HANDLE) {
        std::string msg = "CreateEvent(): " + GetLastError();
        throw std::runtime_error(msg);
    }

    this->workerLoopEvent = CreateEvent(nullptr, false, false, nullptr);
    if (this->workerLoopEvent == (HANDLE)ERROR_INVALID_HANDLE) {
        std::string msg = "CreateEvent(): " + GetLastError();
        throw std::runtime_error(msg);
    }

    // then, the secondary threads
    this->setUpStatsThread();
    this->setUpWorkerThread();

    this->constructTime = std::chrono::steady_clock::now();
}

/**
 * Clean up any dangling resources.
 * 
 * This signals for threads to close gracefully. If they don't, we'll just kill them anyways.
 */
SenderSocket::~SenderSocket() noexcept(false)
{
    // tell the stats/worker thread to go commit sudoku
    if (!SetEvent(this->quitEvent)) {
        std::string msg = "SetEvent(): " + GetLastError();
        throw std::runtime_error(msg);
    }

    // wait for the stats thread and worker to actually die
    if (WaitForSingleObject(this->statsThread, 500) != WAIT_OBJECT_0) {
        std::cerr << "Stats thread failed to exit gracefully; killing it" << std::endl;
        TerminateThread(this->statsThread, 0); // die bitch
    }
    CloseHandle(this->statsThread);
    this->statsThread = INVALID_HANDLE_VALUE;

    if (WaitForSingleObject(this->workerThread, 5000) != WAIT_OBJECT_0) {
        std::cerr << "Worker thread failed to exit gracefully; killing it" << std::endl;
        TerminateThread(this->workerThread, 0);
    }
    CloseHandle(this->workerThread);
    this->workerThread = INVALID_HANDLE_VALUE;

    // clean up handles
    CloseHandle(this->quitEvent);
    CloseHandle(this->abortEvent);

    CloseHandle(this->empty);
    CloseHandle(this->full);
    CloseHandle(this->workerLoopEvent);

    this->quitEvent = INVALID_HANDLE_VALUE;
    this->abortEvent = INVALID_HANDLE_VALUE;
    this->workerLoopEvent = INVALID_HANDLE_VALUE;

    // close socket if still open
    if (this->sock != INVALID_SOCKET) {
        closesocket(this->sock);
        this->sock = INVALID_SOCKET;
    }
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

    // set up the send queue
    this->queue.reserve(window);
    for (size_t i = 0; i < window; i++) {
        this->queue.emplace_back();
    }

    this->full = CreateSemaphore(NULL, 0, (LONG) window, L"SendQueueFull");
    this->empty = CreateSemaphore(NULL, 0, (LONG) window, L"SendQueueEmpty");

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
    this->window = window;
    this->currentSeq = 0;
    this->startTime = std::chrono::steady_clock::now();
    this->rtoDelay = max(kRetransmissionTimeout, (2.0 * ((double) rtt)));

    this->sendPacketRetransmit(&syn, sizeof(SenderSynPacket), kMaxRetransmissionsSYN, true, true, "SYN");

    // if we get here, the connection was successful
    this->synAckTime = std::chrono::steady_clock::now();
    this->isConnected = true;

    // set up stats and worker threads
    ResumeThread(this->workerThread);
    ResumeThread(this->statsThread);
}

/**
 * @brief Closes the connection. A FIN packet is sent before the socket is closed.
*/
void SenderSocket::close()
{
    if (!this->isConnected) {
        throw SocketError(SocketError::kStatusNotConnected);
    } else if (this->isClosing) {
        throw SocketError(SocketError::kStatusNotConnected, "already waiting in close()");
    }
    this->isClosing = true;

    // wait for the queue of pending packets to drain
    while (this->senderBase != this->currentSeq) {
        WaitForSingleObject(this->workerLoopEvent, 25);
    }

    // put the socket back into blocking mode
    WSAEventSelect(this->sock, nullptr, 0);

    u_long no = 0;
    ioctlsocket(this->sock, FIONBIO, &no);

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
void SenderSocket::sendPacketRetransmit(void* data, size_t length, size_t attempts, bool updateRto, bool log, const std::string& kind)
{
    int err;
    double rto = 0;

    // fixed timeout value (for retransmission)
    struct timeval timeout = { 0 };
    timeout.tv_sec = (int)this->rtoDelay;
    timeout.tv_usec = (int)((this->rtoDelay - ((int64_t)this->rtoDelay)) * 1000 * 1000);

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
        this->stats.totalBytesSent += (unsigned long)length;

        if (log && this->debug) {
            auto nowTs = std::chrono::steady_clock::now();
            double now = std::chrono::duration_cast<std::chrono::milliseconds>(nowTs - this->startTime).count() / 1000.f;

            std::cout << "[" << std::fixed << std::setprecision(3) << std::setw(6) << now << "] --> "
                      << kind << ' ' << hdr->seq << " (attempt " << (i + 1) << " of "
                      << attempts << ", RTO " << this->rtoDelay << ")"
                      << " to "
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

        // if SYN-ACK, update semaphore
        if (rxHdr->flags.syn && rxHdr->flags.ack) {
            this->lastReleased = min(this->window, rxHdr->receiveWindow);
            ReleaseSemaphore(this->empty, (LONG) this->lastReleased, nullptr);
        }

        // calculate round-trip time for this packet
        auto receivedAt = std::chrono::steady_clock::now();
        rto = (std::chrono::duration_cast<std::chrono::milliseconds>(receivedAt - sentAt).count() / 1000.f);

        goto success;
    }

    // if we get here, max number of retransmissions exhausted
    throw SocketError(SocketError::kStatusTimeout);

success:;
    // print how long this song and dance took
    auto nowTs = std::chrono::steady_clock::now();
    double now = std::chrono::duration_cast<std::chrono::milliseconds>(nowTs - this->startTime).count() / 1000.f;

    if (log) {
        std::cout << "[" << std::fixed << std::setprecision(3) << std::setw(6) << now << "] <-- "
                  << kind << ((rxHdr->flags.ack) ? "-ACK " : " ") << rxHdr->ackSeq << " window $"
                  << std::hex << std::setw(8) << std::setfill('0') << rxHdr->receiveWindow << std::dec << std::setfill(' ');
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
 * Sends data from the given buffer.
 */
void SenderSocket::send(void* data, size_t length)
{
    // validate we're connected and not attempting to close
    if (!this->isConnected) {
        throw SocketError(SocketError::kStatusNotConnected);
    } else if (this->isClosing) {
        throw SocketError(SocketError::kStatusNotConnected, "socket is closing");
    }

    // wait for quit, connection abort, or empty event
    HANDLE events[] = {
        this->quitEvent, this->abortEvent, this->empty
    };
    DWORD waitRet = WaitForMultipleObjects(3, events, false, INFINITE);

    switch (waitRet) {
        // need to quit
        case WAIT_OBJECT_0:
            throw SocketError(SocketError::kStatusNotConnected);
        // connection got fucked
        case (WAIT_OBJECT_0 + 1):
            throw SocketError(SocketError::kStatusSendFailed);
        // have space to send more packets
        case (WAIT_OBJECT_0 + 2):
            goto queueSpaceAvailable;

        // system errors
        default:
            throw SocketError(SocketError::kStatusSystemError);
    }

    // we jump down here if queue space is available
queueSpaceAvailable:;
    // build the packet
    char buf[kMaxPacketSize];
    memset(&buf, 0, kMaxPacketSize);

    size_t usedPacketLen = (sizeof(SenderPacketHeader) + length);

    auto packet = reinterpret_cast<SenderDataPacket*>(buf);
    packet->header.flags.magic = kFlagsMagic;
    packet->header.seq = this->currentSeq;

    if (this->debug) {
        std::cout << "\tTX: seq " << this->currentSeq << std::endl;
    }

    memcpy(packet->data, data, length);

    // write it into the tx buffer
    size_t slot = this->currentSeq % this->window;

    this->queue[slot].sequence = this->currentSeq;
    this->queue[slot].type = pbuf::kTypeData;
    this->queue[slot].numTx = 0;
    this->queue[slot].payloadSz = usedPacketLen;

    assert(usedPacketLen <= kMaxPacketSize);
    memcpy(this->queue[slot].payload, packet, usedPacketLen);

    // signal that there is a packet to send
    this->currentSeq++;
    ReleaseSemaphore(this->full, 1, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
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
    const int kRxBufSz = (32 * 1000 * 1000), kTxBufSz = (32 * 1000 * 1000);

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
DWORD WINAPI __fucker::WorkerThreadEntry(LPVOID ctx)
{
    static_cast<SenderSocket*>(ctx)->workerThreadMain();
    return 0;
}

/**
 * @brief Creates the worker thread and its data structures
*/
void SenderSocket::setUpWorkerThread()
{
    using namespace __fucker;

    // create suspended thread
    this->workerThread = CreateThread(nullptr, kWorkerStackSize, WorkerThreadEntry, this, CREATE_SUSPENDED, nullptr);
    assert(this->workerThread != INVALID_HANDLE_VALUE);

    SetThreadPriority(this->workerThread, THREAD_PRIORITY_TIME_CRITICAL);
}


/**
 * @brief Main loop of the worker thread; this pulls packets out of the send queue as they arrive
 * and goes to send them.
*/
void SenderSocket::workerThreadMain()
{
    int err;

    // create the event handle for the socket
    assert(this->sockEvent == INVALID_HANDLE_VALUE);

    this->sockEvent = CreateEvent(nullptr, false, false, nullptr);
    assert(this->sockEvent != INVALID_HANDLE_VALUE);

    // set up socket to use the event
    err = WSAEventSelect(this->sock, this->sockEvent, FD_READ);
    if (err == SOCKET_ERROR) {
        throw SocketError(SocketError::kStatusSystemError, WSAGetLastError());
    }

    // events to wait on
    HANDLE handles[] = {
        this->full, this->sockEvent
    };

    // while transfer is ongoing
    try {
        while (this->isConnected) {
            // calculate timeout
            DWORD timeout = INFINITE;

            // wait for timeout, shit in the queue, or a packet
            err = WaitForMultipleObjects(2, handles, false, timeout);

            switch (err) {
            // packet to transmit
            case WAIT_OBJECT_0:
                this->workerDrainQueue();
                break;
            // data available to read
            case (WAIT_OBJECT_0 + 1):
                this->workerReadAck();
                break;

            // timed out waiting for an ack; re-transmit the packet
            case WAIT_TIMEOUT:
                break;

            // system errors
            default:
                throw SocketError(SocketError::kStatusSystemError);
            }
        }

        SetEvent(this->workerLoopEvent);
    } catch (const std::exception& e) {
        std::cerr << "WorkerThread exception: " << e.what() << std::endl;
        return;
    }
}

/**
 * @brief Reads the packet from the top of the queue and transmits it.
*/
void SenderSocket::workerDrainQueue()
{
    int err;

    // pull out the packet
    size_t slot = this->nextToSend % this->window;
    auto& packet = this->queue[slot];

    // transmit packet
    packet.txTime = std::chrono::steady_clock::now();

    err = sendto(this->sock, (const char*) packet.payload, (int) packet.payloadSz, 0,
        (struct sockaddr*)&this->host, sizeof(struct sockaddr_in));
    if (err == -1) {
        throw SocketError(SocketError::kStatusSendFailed, WSAGetLastError());
    }

    packet.numTx++;
    this->stats.totalBytesSent += (unsigned long) packet.payloadSz;

    // increment for next packet
    this->nextToSend++;
}

/**
 * @brief Reads pending data from the socket.
*/
void SenderSocket::workerReadAck()
{
    int err;

    // packet to read
    static const size_t kReceiveSz = kMaxPacketSize;
    char receive[kReceiveSz] = { 0 };
    ReceiverPacketHeader* rxHdr = reinterpret_cast<ReceiverPacketHeader*>(receive);

    struct sockaddr_storage respFrom;
    socklen_t respFromLen = sizeof(struct sockaddr_storage);

    // receive response
    err = recvfrom(this->sock, receive, kReceiveSz, 0, (struct sockaddr*)&respFrom, &respFromLen);
    if (err == -1) {
        throw SocketError(SocketError::kStatusRecvFailed, WSAGetLastError());
    }
    assert(err >= sizeof(ReceiverPacketHeader));

    // packet was an acknowledgement
    if (rxHdr->flags.ack) {
        if (this->debug || true) {
            std::cout << "\tTX: received ack for seq " << rxHdr->ackSeq << ", window "
                      << rxHdr->receiveWindow << std::endl;
        }

        // move sender base if the ack is beyond what we've sent
        if (rxHdr->ackSeq > this->senderBase) {
            this->senderBase = rxHdr->ackSeq;
            size_t effectiveWin = min(this->window, rxHdr->receiveWindow);

            // advance semaphore to allow more sending
            size_t newReleased = this->senderBase + effectiveWin - this->lastReleased;
            ReleaseSemaphore(this->empty, (LONG) newReleased, nullptr);
            this->lastReleased += newReleased;
        }

        /*
        // calculate actual round trip time
        auto receivedAt = std::chrono::steady_clock::now();
        this->dataAckTime = receivedAt;

        sampleRtt = (std::chrono::duration_cast<std::chrono::milliseconds>(receivedAt - sentAt).count() / 1000.f);

        // update estimated RTT
        if (this->estimatedRttLast <= 0) {
            this->estimatedRttLast = sampleRtt;
        } else {
            this->estimatedRttLast = this->estimatedRtt;
        }
        this->estimatedRtt = ((1.f - kRttAlpha) * this->estimatedRttLast) + (kRttAlpha * sampleRtt);

        // update RTT deviation
        if (this->devRttLast <= 0) {
            this->devRttLast = 0;
        } else {
            this->devRttLast = this->devRtt;
        }

        this->devRtt = ((1 - kRttBeta) * this->devRttLast) + (kRttBeta * fabs(sampleRtt - this->estimatedRtt));

        this->rtoDelay = this->estimatedRtt + (4 * max(this->devRtt, 0.010));*/

        // ack was valid
        // this->stats.payloadBytesAcked += (unsigned long)length;
    }
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

    // set up thread
    this->statsThread = CreateThread(nullptr, kStatsStackSize, StatsThreadEntry, this, CREATE_SUSPENDED, nullptr);
    assert(this->statsThread != INVALID_HANDLE_VALUE);

    SetThreadPriority(this->statsThread, THREAD_PRIORITY_ABOVE_NORMAL);
}

/**
 * @brief Main loop for the stats thread
*/
void SenderSocket::statsThreadMain()
{
    // prepare stats structures
    this->stats.lastPrint = std::chrono::steady_clock::now();

    // wait on the quit event
    while (WaitForSingleObject(this->quitEvent, 2000) == WAIT_TIMEOUT) {
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
    using namespace std::chrono;

    auto now = steady_clock::now();

    // seconds since the last stats computation
    auto secsSinceLastStats = duration_cast<microseconds>(now - this->stats.lastPrint).count() / 1000.f / 1000.f;
    this->stats.lastPrint = now;

    // seconds since constructyboi
    auto secs = duration_cast<seconds>(now - this->constructTime).count();

    out << "[" << std::setw(2) << secs << "] ";

    // sender base and mbytes acked
    size_t bytesAcked = this->currentSeq * kMaxPacketSize;
    double mbAcked = ((double)bytesAcked) / 1000.f / 1000.f;
    out << "B " << std::setw(6) << this->currentSeq << " (" << std::setw(6) << mbAcked
        << " MB) ";

    // next sequence number
    out << "N " << std::setw(6) << (this->currentSeq + 1) << ' ';

    // timeout/fast retransmit/effective window size
    out << "T " << std::setw(2) << this->stats.timeout << ' '
        << "F " << std::setw(2) << this->stats.fastReTx << ' '
        << "W " << std::setw(2) << this->stats.effectiveWindow << ' ';

    // goodput and RTT
    size_t acked = this->stats.payloadBytesAcked;
    size_t bytesDiff = acked - this->stats.bytesConsumed;
    this->stats.bytesConsumed = (unsigned long) acked;

    double goodput = ((double)bytesDiff * 8) / 1000.f / 1000.f / secsSinceLastStats;

    out << "S " << std::setw(6) << goodput << " Mbps "
        << " RTT " << std::setw(5) << std::fixed << std::setprecision(3) << this->estimatedRtt;

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