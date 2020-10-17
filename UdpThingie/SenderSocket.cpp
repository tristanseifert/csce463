#include "pch.h"
#include "SenderSocket.h"

#include <string>
#include <sstream>
#include <unordered_map>

///////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * @brief Attempts to open a connection to the remote host.
 */
void SenderSocket::open(const std::string& host, uint16_t port, size_t window, float rtt, float speed, float loss[2])
{
    int err;

    // resolve address
    struct sockaddr_storage storage;
    memset(&storage, 0, sizeof(struct sockaddr_storage));

    SenderSocket::resolve(host, &storage);

    // send the SYN packet
}

/**
 * @brief Closes the connection. A FIN packet is sent before the socket is closed.
*/
void SenderSocket::close()
{
    // TODO: implement
}

///////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Sends data from the given buffer.
 */
void SenderSocket::send(void* data, size_t length)
{
    // TODO: implement
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