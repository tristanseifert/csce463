#ifndef HTTP_H
#define HTTP_H

#include "URL.h"

/**
 * @brief A very basic HTTP client that can connect to a remote server, and
 *		  download data.
*/
class HTTPClient {
public:
    struct Response {
        friend class HTTPClient;

        public:
            Response() { }
            Response(const URL& url) : url(url) { }
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

        private:
            /// URL from which content was fetched
            URL url;

            /// Length of payload, in bytes
            size_t payloadSize = 0;
            /// Payload buffer, previously allocated by malloc()
            void* payload = nullptr;
            /// HTTP status code
            int status = -1;
    };

public:
    HTTPClient();
    virtual ~HTTPClient();

    void connect(sockaddr_in& addr);
    Response fetch(const URL& url);

    

private:
    void sendGet(const URL&);

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
};

#endif