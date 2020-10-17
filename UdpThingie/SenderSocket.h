#ifndef SENDERSOCKET_H
#define SENDERSOCKET_H

#include <cstdint>
#include <cstddef>

#include <stdexcept>
#include <string>
#include <unordered_map>

/**
 * @brief Implements the fancy schmanzy stuff on top of UDP to go very very fast
*/
class SenderSocket {
public:
	/// Port number in use
    constexpr static const uint16_t kPortNumber = 22345;
	/// Maximum packet size
    constexpr static const size_t kMaxPacketSize = (1500 - 28);

public:
    /**
     * @brief Errors produced as a result of socket IO calls. These are propagated out from the
     * appropriate functions as exceptions of this type.
    */
    class SocketError : public std::exception {
        friend class SenderSocket;

    public:
        /**
         * @brief Various error types caused by the class.
         * 
         * Note that all system errors (send/recv failed) carry additional info.
         */
        enum Type {
            /// No error
            kStatusOk = 0,
            /// Attempt to connect on an already connected socket
            kStatusConnected = 1,
            /// Attempt to send/receive on a disconnected socket
            kStatusNotConnected = 2,
            /// Invalid target hostname or IP address
            kStatusInvalidHost = 3,
            /// Internal error sending data
            kStatusSendFailed = 4,
            /// Timeout on retransmissions
            kStatusTimeout = 5,
            /// Internal error receiving data
            kStatusRecvFailed = 6,
        };

        /// Gets the type of exception
        const Type getType() const
        {
            return this->type;
        }

        /// Descriptive message
        virtual const char* what() noexcept
        {
            return this->message.c_str();
        }

    private:
        SocketError(Type);
        SocketError(Type t, size_t code)
            : SocketError(t, std::to_string(code)) {};
        SocketError(Type, const std::string&);

    private:
        std::string message;
        Type type;

    private:
        static const std::unordered_map<Type, std::string> kDefaultMessages;
    };

public:
    void open(const std::string& host, uint16_t port, size_t window, float rtt, float speed, float loss[2]);
    void close();

    void send(void* data, size_t length);

private:
    static void resolve(const std::string &host, struct sockaddr_storage* outAddr);
};

#endif