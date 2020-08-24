#include "pch.h"
#include "URL.h"
#include "HTTPClient.h"

#include <iostream>
#include <string>
#include <sstream>
#include <stdexcept>
#include <regex>

/**
 * @brief Parses the first line of the HTTP response to extract 1) the HTTP
 *        version; 2) status code, and 3) status text.
*/
const std::regex HTTPClient::kStatusRegex = std::regex(R"blaze(^(?:HTTP\/)(\d+\.\d+)(?:\s+)(\d+)(?:\s+)([^\r\n]+))blaze");

/**
 * @brief Used to parse HTTP headers into key/value pairs.
*/
const std::regex HTTPClient::kHeaderRegex = std::regex(R"blaze(^([^:]+)(?:\:\s*)([^\r\n]+))blaze");

/**
 * @brief Trims whitespace from the start and end of the string
 * @param s String to trim
 * @return String sans whitespace
*/
static std::string trim(std::string s) {
    std::regex e("^\\s+|\\s+$"); // remove leading and trailing spaces
    return std::regex_replace(s, e, "");
}

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
    this->parseHeaders(resp, readBuf, readBufSz);
    this->extractPayload(resp, readBuf, readBufSz);

    free(readBuf);

    // done!
    return resp;
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
 * @brief Parses the headers of the HTTP response, including the status code.
 * @param response Object to populate with headers
 * @param read Buffer of read data
 * @param readSz Size of the read data buffer
 * 
 * @note This is technically _not_ conforming to the standards w.r.t. multiple
 *       headers with the same value.
*/
void HTTPClient::parseHeaders(Response &response,  const void *read, const size_t readSz)
{
    using namespace std;

    // get the header string only
    const size_t payloadIndex = getPayloadIndex(read, readSz);

    auto headerStr = string(reinterpret_cast<const char*>(read), payloadIndex);
    response.responseHeader = trim(headerStr);

    // parse the HTTP status line
    auto statusEndIdx = headerStr.find(kHeaderNewline);
    if (statusEndIdx == string::npos) {
        throw runtime_error("no status line");
    }
    auto statusLine = string(headerStr.begin(), (headerStr.begin() + statusEndIdx));

    auto statusMatch = smatch();
    if (!regex_search(statusLine, statusMatch, kStatusRegex)) {
        throw runtime_error("invalid status line");
    }

    if (statusMatch[1] != "1.0" && statusMatch[1] != "1.1") {
        throw runtime_error("invalid HTTP version: '" + statusMatch[1].str() + "'");
    }

    unsigned long status = strtoul(statusMatch[2].str().c_str(), nullptr, 10);
    if (status < 100 || status > 599) {
        throw runtime_error("invalid HTTP status: " + statusMatch[2].str());
    }

    response.status = status;

    // parse the headers
    auto headersOnly = headerStr.substr(statusEndIdx + 2);

    regex_token_iterator<string::iterator> headersEnd;
    regex_token_iterator<string::iterator> headers(headersOnly.begin(), headersOnly.end(), kHeaderRegex, {1, 2});

    while (headers != headersEnd) {
        std::string name = *headers++;
        std::string value = *headers++;

        response.headers[name] = value;
    }
}

/**
 * @brief Extracts the payload of the HTTP response.
 * @param response Object to populate with dat
 * @param read Buffer of read data
 * @param readSz Size of the read data buffer
*/
void HTTPClient::extractPayload(Response &response, const void *read, const size_t readSz)
{
    const size_t payloadIndex = getPayloadIndex(read, readSz);
    const size_t payloadSize = readSz - payloadIndex;

    if (payloadSize == 0 || payloadSize > readSz) {
        throw std::runtime_error("no payload in request");
    }

    // allocate a payload buffer and copy it
    void* buf = malloc(payloadSize);
    if (!buf) {
        throw std::runtime_error("malloc()");
    }

    memcpy(buf, (reinterpret_cast<const char *>(read) + payloadIndex), 
           payloadSize);

    response.payloadSize = payloadSize;
    response.payload = buf;
}

/**
 * @brief Finds the first byte of payload data.
 * @param _in Buffer of read data
 * @param readSz Size of the read data buffer
 * @return Index of the first payload byte
 * @throws If two consecutive newline sequences ("\r\n\r\n") aren't found
*/
size_t HTTPClient::getPayloadIndex(const void*_in, const size_t readSz)
{
    const char* in = static_cast<const char *>(_in);

    for (size_t i = 0; i < (readSz - 3); i++) {
        // read 4 bytes and check if two newlines are contained within
        uint32_t temp = *(reinterpret_cast<const uint32_t*>(in + i));

        if (temp == '\r\n\r\n') {
            return i;
        }
    }

    throw std::runtime_error("missing CRLF+CRLF");
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
            WSACloseEvent(event);
            throw std::runtime_error("IO timed out");
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
        this->payload = nullptr;
    }
}

/**
 * @brief Releases the payload of the response.
*/
void HTTPClient::Response::release()
{
    if (this->payload) {
        free(this->payload);
        this->payload = nullptr;
    }
}