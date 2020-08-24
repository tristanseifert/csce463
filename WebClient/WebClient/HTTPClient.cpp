#include "pch.h"
#include "URL.h"
#include "HTTPClient.h"

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

    // create a response object
    return resp;
}

/**
 * @brief Sends a GET request for the given url.
 * @param  URL to request; only the path and query string are sent.
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
