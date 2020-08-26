#ifndef HTTP_H
#define HTTP_H

#include "URL.h"

#include <unordered_map>
#include <regex>

namespace webclient {
/**
 * @brief A very basic HTTP client that can connect to a remote server, and
 *		  download data.
*/
class HTTPClient {
public:
    struct Response {
        friend class HTTPClient;

        public:
            inline Response() { this->payload = nullptr; }
            inline Response(const URL& url) : url(url) { }
            virtual ~Response();

            size_t getPayloadSize() const
            {
                return this->payloadSize;
            }
            void* getPayload() const
            {
                return this->payload;
            }
            int getStatus() const
            {
                return this->status;
            }

            bool hasHeader(const std::string name) const
            {
                return this->headers.find(name) != this->headers.end();
            }
            std::string getHeader(const std::string name) const
            {
                return this->headers.at(name);
            }

            std::string getResponseHeader() const
            {
                return this->responseHeader;
            }

            URL getUrl() const
            {
                return this->url;
            }

            void release();

        private:
            /// URL from which content was fetched
            URL url;

            /// Length of payload, in bytes
            size_t payloadSize = 0;
            /// Payload buffer, previously allocated by malloc()
            void* payload = nullptr;
            /// HTTP status code
            int status = -1;

            /// Raw string for the HTTP response (minus payload)
            std::string responseHeader;
            /// HTTP headers
            std::unordered_map<std::string, std::string> headers;
    };

public:
    HTTPClient();
    virtual ~HTTPClient();

    void connect(sockaddr& addr);
    Response fetch(const URL& url);    

private:
    void sendGet(const URL&);
    void parseHeaders(Response&, const void*, const size_t);
    void extractPayload(Response&, const void*, const size_t);

    static size_t getPayloadIndex(const void*, const size_t);

    static void* readUntilEnd(SOCKET, size_t *);

    static void send(SOCKET, const std::string&);
    static void send(SOCKET, const void*, size_t);

private:
    SOCKET sock = INVALID_SOCKET;

private:
    /// Read timeout (in ms)
    static const unsigned int kRxTimeout = (5 * 1000);
    /// Default receive buffer size, in bytes
    static const size_t kInitialRxBufSize = (32 * 1024);
    /// Number of bytes by which the receive buffer should grow
    static const size_t kRxBufferSizeGrowth = (32 * 1024);
    /// User agent string to present in all requests
    static inline const auto kUserAgent = "blazerino/1.1";
    static inline const auto kHeaderNewline = "\r\n";

    static const std::regex kStatusRegex;
    static const std::regex kHeaderRegex;
};

}

#endif