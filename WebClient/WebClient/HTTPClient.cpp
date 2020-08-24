#include "pch.h"
#include "URL.h"
#include "HTTPClient.h"

#include <iostream>
#include <string>
#include <sstream>
#include <stdexcept>

/**
 * @brief Initializes a new HTTP client.
*/
HTTPClient::HTTPClient()
{
}

/**
 * @brief Cleans up the HTTP client.
*/
HTTPClient::~HTTPClient()
{
    int err;

    // close socket if not already done
    if (this->sock != INVALID_SOCKET) {
        err = closesocket(this->sock);
        // XXX: can't really do anything with errors

        this->sock = INVALID_SOCKET;
    }
}

/**
 * @brief Connects to the remote server.
 * @param addr Address to connect to
*/
void HTTPClient::connect(sockaddr_in& addr)
{
    int err;

    // create the socket
    this->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (this->sock == INVALID_SOCKET) {
        auto errStr = std::to_string(WSAGetLastError());
        throw std::runtime_error("socket() " + errStr);
    }

    // attempt to connect
    err = ::connect(this->sock, (struct sockaddr*) &addr, sizeof(struct sockaddr_in));
    if (err == SOCKET_ERROR) {
        auto errStr = std::to_string(WSAGetLastError());
        throw std::runtime_error("connect() " + errStr);
    }
}

/**
 * @brief Fetches the contents of the specified url from the server.
 * @param url Url to fetch
 * @return HTTP response (including payload/headers)
*/
HTTPClient::Response HTTPClient::fetch(const URL& url)
{
    Response resp;

    // make sure socket is connected & valid
    if (this->sock == INVALID_SOCKET) {
        throw std::runtime_error("invalid socket");
    }

    // send the GET request
    this->sendGet(url);

    // read data until the entire request is read in
    size_t readBufSz = 0;
    void* readBuf = readUntilEnd(this->sock, &readBufSz);

    // parse headers and extract payload
    free(readBuf);

    // done!
    return resp;
}

/**
 * @brief Reads from the remote socket until the connection is closed, or a
 *        timeout expires.
 * @param sock Socket to read from
 * @param written How many bytes were read from the socket
 * @return Buffer containing the response, or NULL. Caller is responsible for
 *         calling free() on the buffer.
*/
void *HTTPClient::readUntilEnd(SOCKET sock, size_t *written)
{
    int err;
    WSANETWORKEVENTS events;

    // allocate the initial read buffer
    size_t bufOff = 0;
    size_t bufSz = kInitialRxBufSize;
    char* buf = static_cast<char *>(malloc(bufSz));
    if (!buf) {
        throw std::runtime_error("malloc()");
    }

    // set up for events
    WSAEVENT event = WSACreateEvent();
    if (event == WSA_INVALID_EVENT) {
        auto errStr = std::to_string(WSAGetLastError());
        throw std::runtime_error("WSACreateEvent() " + errStr);
    }

    err = WSAEventSelect(sock, event, FD_READ | FD_CLOSE);
    if (err == SOCKET_ERROR) {
        auto errStr = std::to_string(WSAGetLastError());
        throw std::runtime_error("WSAEventSelect() " + errStr);
    }

    // continuously read from the socket until closed or timeout
    while (true) {
        // wait for an event
        err = WSAWaitForMultipleEvents(1, &event, true, kRxTimeout, false);

        if (err == WSA_WAIT_FAILED) {
            auto errStr = std::to_string(WSAGetLastError());
            throw std::runtime_error("WSAWaitForMultipleEvents() " + errStr);
        }
        else if (err == WSA_WAIT_TIMEOUT) {
 //           goto closed;
            WSACloseEvent(event);
            throw std::runtime_error("read timed out");
        } 
        else if (err != WSA_WAIT_EVENT_0) {
            continue;
        }

        // determine the nature of event
        err = WSAEnumNetworkEvents(sock, event, &events);
        if (err == SOCKET_ERROR) {
            auto errStr = std::to_string(WSAGetLastError());
            throw std::runtime_error("WSAEnumNetworkEvents() " + errStr);
        }

        // did the socket close?
        if (events.lNetworkEvents & FD_CLOSE) {
            goto closed;
        }
        else if (!(events.lNetworkEvents & FD_READ)) {
            continue;
        }

        // resize the buffer if needed
        size_t toRead = bufSz - bufOff;

        if (toRead == 0) {
            bufSz += kRxBufferSizeGrowth;

            char* newBuf = static_cast<char *>(realloc(buf, bufSz));
            if (!newBuf) {
                free(buf);
                throw std::runtime_error("realloc()");
            }

            buf = newBuf;
            toRead = bufSz - bufOff;
        }

        // read however many bytes were left
        err = recv(sock, (buf + bufOff), toRead, 0);

        if (err == SOCKET_ERROR) {
            auto errStr = std::to_string(WSAGetLastError());
            throw std::runtime_error("recv() " + errStr);
        } else if (err == 0) {
            // connection was closed
            std::cout << "Connection closed" << std::endl;
            goto closed;
        }

        // increment the write pointeri
        bufOff += err;
    }

    // finished reading
closed:;

    // zero terminate the buffer
    if ((bufSz - bufOff) > 0) {
        buf[bufOff] = '\0';
    } else {
        bufSz += 1;
        char* newBuf = static_cast<char*>(realloc(buf, bufSz));
        if (!newBuf) {
            free(buf);
            throw std::runtime_error("realloc()");
        }
        buf = newBuf;
        buf[bufOff] = '\0';
    }

    // clean-up
beach:;
    WSACloseEvent(event);

    if (written) {
        *written = bufOff;
    }

    return buf;
}

/**
 * @brief Sends a GET request for the given url.
 * @param url URL to request; only the path and query string are sent.
 * 
 * This performs a HTTP 1.0 GET request.
*/
void HTTPClient::sendGet(const URL& url)
{
    std::stringstream request;

    // write the path and host
    request << "GET " << url.getPath();

    if (!url.getQuery().empty()) {
        request << "?" << url.getQuery();
    }

    request << " HTTP/1.0" << kHeaderNewline;

    request << "Host: " << url.getHostname() << kHeaderNewline;

    // remaining headers
    request << "User-agent: " << kUserAgent << kHeaderNewline;
    request << "Connection: close" << kHeaderNewline;
    request << kHeaderNewline;

    // send the header
    auto str = request.str();
    HTTPClient::send(this->sock, str);
}


/**
 * @brief Sends the entire buffer provided over the socket.
 * 
 * @param sock Socket to send data to
 * @param buf Buffer of data to send
 * @param bufSz Number of bytes in buffer to send
*/
void HTTPClient::send(SOCKET sock, const void* buf, size_t bufSz)
{
    int err;

    // try to send
    err = ::send(sock, static_cast<const char *>(buf), bufSz, 0);

    // handle error cases
    if (err == SOCKET_ERROR) {
        auto errStr = std::to_string(WSAGetLastError());
        throw std::runtime_error("send() " + errStr);
    } else if (err != bufSz) {
        throw std::runtime_error("incomplete send: wrote " + std::to_string(err)
                                 + ", expected " + std::to_string(bufSz));
    }
}

void HTTPClient::send(SOCKET sock, const std::string& str)
{
    HTTPClient::send(sock, str.c_str(), str.length());
}


/**
 * @brief Deallocates a HTTP response.
*/
HTTPClient::Response::~Response()
{
    // release payload if it was allocated
    if (this->payload) {
        free(this->payload);
        this->payload = nullptr;
    }
}
