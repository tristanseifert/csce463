#ifndef SENDERSOCKET_H
#define SENDERSOCKET_H

#include <cstdint>
#include <cstddef>

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <chrono>

/**
 * @brief Implements the fancy schmanzy stuff on top of UDP to go very very fast
*/
class SenderSocket {
public:
	/// Port number in use
    constexpr static const uint16_t kPortNumber = 22345;
	/// Maximum packet size
    constexpr static const size_t kMaxPacketSize = (1500 - 28);
    /// Max number of retransmissions (for connection establishment SYN packets)
    constexpr static const size_t kMaxRetransmissionsSYN = 3;
    /// Max number of retransmissions (for all other types of packets)
    constexpr static const size_t kMaxRetransmissions = 5;
    /// Default retransmission timeout (in seconds)
    constexpr static const double kRetransmissionTimeout = 1.0;

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
            /// Some undefined system error took place
            kStatusSystemError = 7,
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

private:
    /// Socket used for communicating
    SOCKET sock = INVALID_SOCKET;
    /// Destination host
    struct sockaddr_storage host = { 0 };
    /// String version of destination host
    std::string hostStr = "";

    /// if set, we've established a connection before
    bool isConnected = false;
    /// time at which the connection was begun to be established
    std::chrono::steady_clock::time_point startTime;
    /// current retransmission delay
    double rtoDelay = kRetransmissionTimeout;
    /// current sequence number. incremented on every transmission
    DWORD currentSeq = 0;

public:
    virtual ~SenderSocket();

    void open(const std::string& host, uint16_t port, size_t window, float rtt, float speed, float loss[2]);
    void close();

    void send(void* data, size_t length);

private:
    void setUpSocket(struct sockaddr_storage* addr);
    void sendPacketRetransmit(void* data, size_t length, size_t numRetrans = kMaxRetransmissions, bool updateRto = false, bool log = false, const std::string& kind = "");

private:
    static void resolve(const std::string &host, struct sockaddr_storage* outAddr);
};

#endif