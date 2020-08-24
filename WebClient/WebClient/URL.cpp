#include "pch.h"
#include "URL.h"

#include <string>
#include <regex>
#include <stdexcept>
#include <algorithm>

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
URL::~URL()
{

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
    this->scheme = result[2];

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
        std::string lowerScheme = this->scheme;
        transform(lowerScheme.begin(), lowerScheme.end(), lowerScheme.begin(), 
            [](unsigned char c) {
                return std::tolower(c); 
        });

        this->port = kPortMap.at(lowerScheme);
    }
}
