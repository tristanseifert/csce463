#ifndef SENDERSOCKET_H
#define SENDERSOCKET_H

#include <cstdint>
#include <cstddef>

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <chrono>
#include <atomic>
#include <ostream>

namespace __fucker {
    DWORD WINAPI StatsThreadEntry(LPVOID);
}

/**
 * @brief Implements the fancy schmanzy stuff on top of UDP to go very very fast
*/
class SenderSocket {
    friend DWORD WINAPI __fucker::StatsThreadEntry(LPVOID);


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

public:
    /// Returns the time at which connection establishment began
    std::chrono::steady_clock::time_point getStartTime() const
    {
        return this->startTime;
    }

    /// Returns the time at which the SYN-ACK to establish the connection was received
    std::chrono::steady_clock::time_point getSynAckTime() const
    {
        return this->synAckTime;
    }

private:
    /// size of the stats thread stack, in bytes
    constexpr static const size_t kStackSize = (1024 * 128);

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
    /// time at which the SYN-ACK was received
    std::chrono::steady_clock::time_point synAckTime;
    /// time the constructor was called
    std::chrono::steady_clock::time_point constructTime;

    /// current retransmission delay
    double rtoDelay = kRetransmissionTimeout;
    /// current sequence number. incremented on every transmission
    DWORD currentSeq = 0;

    /// When set, debug logging is on.
    bool debug = false;

    /// handle to the stats thread
    HANDLE statsThread = INVALID_HANDLE_VALUE;
    /// signalled when quit is desired
    HANDLE quitEvent = INVALID_HANDLE_VALUE;

    /// Current stats to print for the stats thread
    struct {
        /// Sender base
        std::atomic_ulong senderBase = 0;
        /// Number of ACKed packets
        std::atomic_ulong packetsAcked = 0;
        /// Next sequence number
        std::atomic_ulong nextSeq = 0;
        /// Number of timeouts
        std::atomic_ulong timeout = 0;
        /// Number of fast retransmitted
        std::atomic_ulong fastReTx = 0;
        /// effective window size
        std::atomic_ulong effectiveWindow = 1;
        /// goodput (bytes/sec)
        std::atomic_ulong goodput = 0;
        /// estimated RTT (in msec)
        std::atomic_ulong estimatedRtt = 0;
    } stats;

public:
    SenderSocket();
    virtual ~SenderSocket() noexcept(false);

    void open(const std::string& host, uint16_t port, size_t window, float rtt, float speed, float loss[2]);
    void close();

    void send(void* data, size_t length);

private:
    void setUpSocket(struct sockaddr_storage* addr);
    void sendPacketRetransmit(void* data, size_t length, size_t numRetrans = kMaxRetransmissions, bool updateRto = false, bool log = false, const std::string& kind = "");

private:
    static void resolve(const std::string &host, struct sockaddr_storage* outAddr);

private:
    void setUpStatsThread();
    void statsThreadMain();
    void statsThreadPrint(std::ostream &out, bool newline = true);
};

#endif