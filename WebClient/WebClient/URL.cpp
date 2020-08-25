#include "pch.h"
#include "URL.h"

#include <iostream>
#include <sstream>
#include <string>
#include <regex>
#include <stdexcept>
#include <algorithm>

using namespace webclient;

/**
 * @brief Regex used to parse URLs. This was taken from RFC 3986.
*/
const std::regex URL::kUrlRegex = std::regex("^(([^:/?#]+):)?(//([^/?#]*))?([^?#]*)(\\?([^#]*))?(#(.*))?");
/**
 * @brief Regex used to extract a port number from a "host:port" string. First
 * match is hostname, second is port. If no match is found, no port number was
 * provided.
*/
const std::regex URL::kHostRegex = std::regex("(.+)(?:\\:)(\\d+)$");

/**
 * @brief Provide a mapping from url protocol to port number. This is used
 * when the URL does not explicitly provide a port.
*/
const std::unordered_map<std::string, unsigned int> URL::kPortMap = {
    { "http", 80 },
    { "https", 443 }
};

/**
 * @brief Allocates a new URL by parsing the given string.
 * @param str Input string to parse
*/
URL::URL(const std::string str)
{
    this->parse(str);
}

/**
 * @brief Deallocates all previously allocated resources for the URL.
*/
URL::~URL() { }

/**
 * @brief Resolves the hostname of the URL and populates an address struct.
 * @param outAddr Address struct to hold the address
*/
void URL::resolve(sockaddr_in* outAddr)
{
    URL::resolve(this->hostname, outAddr);
    outAddr->sin_port = htons(this->port);
}
/**
 * @brief Parse the given URL string to fill in all of the parameters in the
 *        class.
 * @param in URL string
 * @throws Exceptions are thrown if required components are missing, or the
 *		   input is not an URL.
*/
void URL::parse(const std::string& in)
{
    using namespace std;

	// run the regex
    auto result = smatch();
    if (!regex_match(in, result, kUrlRegex)) {
        throw std::invalid_argument("Input is not an URL");
    }

    // extract fields that need no further processing
    std::string lowerScheme = result[2];
    transform(lowerScheme.begin(), lowerScheme.end(), lowerScheme.begin(),
        [](unsigned char c) {
            return std::tolower(c);
        });

    this->scheme = result[2];

    if (this->scheme.length() == 0) {
        throw std::runtime_error("Invalid scheme");
    }

    if (result[5].length()) {
        this->path = result[5];
    } else {
        this->path = "/";
    }

    if (result[7].length()) {
        this->query = result[7];
    }
    
    if (result[9].length()) {
        this->fragment = result[9];
    }

    // extract the port number from the host string
    const std::string hostStr = result[4];
    auto hostMatch = smatch();
    
    if (regex_match(hostStr, hostMatch, kHostRegex)) {
        this->hostname = hostMatch[1];

        unsigned long port = strtoul(hostMatch[2].str().c_str(), nullptr, 10);
        if (port == 0 || port > UINT16_MAX) {
            throw std::runtime_error("Invalid port number");
        }
        this->port = port;
    } else {
        // assume no port number; use hostname as-is
        this->hostname = hostStr;
    }

    // if port wasn't set, try to get the default port for the scheme
    if (!this->port) {
        try {
            this->port = kPortMap.at(this->scheme);
        } catch (std::exception) {
            throw std::runtime_error("Invalid scheme");
        }
    }
}


/**
 * @brief Gets a string representation of the URL.
 * @return Stringified URL
*/
std::string URL::toString() const
{
    std::stringstream out;

    // first, the URL scheme
    out << this->scheme;
    out << "://";

    // TODO: username/password

    // then, hostname and port
    out << this->hostname;

    
    try {
        auto defaultPort = kPortMap.at(this->scheme);

        if (defaultPort != this->port) {
            // using nonstandard port
            out << ":";
            out << this->port;
        }
    } catch (std::exception) {
        // we don't know this scheme so include the port always
        out << ":";
        out << this->port;
    }

    // lastly, the path
    out << this->path;

    // TODO: query, fragment

    return out.str();
}

/**
 * @brief Attempts to resolve the given hostname.
 * @param host Hostname to look up (maybe string IP address)
 * @param outAddr Address struct to hold the result
*/
void URL::resolve(const std::string host, sockaddr_in* outAddr)
{
    int err;

    // common setup of the output address
    outAddr->sin_family = AF_INET;

    // try to parse as IP string
    IN_ADDR resolved = { 0 };
    err = inet_pton(AF_INET, host.c_str(), &resolved);

    if (err == 1) {
        outAddr->sin_addr = resolved;
        return;
    } else if (err == -1) {
        throw std::runtime_error("inet_pton(): " + std::to_string(WSAGetLastError()));
    }

    // hints for lookup
    struct addrinfo hints = { 0 };
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    // otherwise, we need to perform a DNS lookup
    struct addrinfo* result = nullptr;
    struct addrinfo* ptr = nullptr;

    err = getaddrinfo(host.c_str(), nullptr, &hints, &result);
    if (err != 0) {
        throw std::runtime_error("getaddrinfo(): " + std::to_string(err));
    }

    // find the first address that's IPv4
    for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {
        if (ptr->ai_family == AF_INET) {
            memcpy((void*)&(outAddr->sin_addr), ptr->ai_addr, ptr->ai_addrlen);
            goto beach;
        }
    }

    // cleanup
beach:;
    freeaddrinfo(result);
}