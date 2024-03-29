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
    DWORD WINAPI WorkerThreadEntry(LPVOID);
}

/**
 * @brief Implements the fancy schmanzy stuff on top of UDP to go very very fast
*/
class SenderSocket {
    friend DWORD WINAPI __fucker::StatsThreadEntry(LPVOID);
    friend DWORD WINAPI __fucker::WorkerThreadEntry(LPVOID);


public:
	/// Port number in use
    constexpr static const uint16_t kPortNumber = 22345;
	/// Maximum packet size
    constexpr static const size_t kMaxPacketSize = (1500 - 28);
    // constexpr static const size_t kMaxPacketSize = (9000 - 28);
    /// Max number of retransmissions (for connection establishment SYN packets)
    constexpr static const size_t kMaxRetransmissionsSYN = 50;
    /// Max number of retransmissions (for all other types of packets)
    constexpr static const size_t kMaxRetransmissions = 50;
    /// Default retransmission timeout (in seconds)
    constexpr static const double kRetransmissionTimeout = 1.0;
    /// Weight of estimated RTT (alpha)
    constexpr static const double kRttAlpha = 0.125;
    /// Estimated difference between SampleRTT/EstimatedRTT (beta)
    constexpr static const double kRttBeta = 0.25;

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

    /// Returns the time at which the most recent ACK for a data packet was received
    std::chrono::steady_clock::time_point getDataAckTime() const
    {
        return this->dataAckTime;
    }

    /// Returns the currently estimated RTT
    double getEstimatedRtt() const
    {
        return this->estimatedRtt;
    }

    /// Total number of acknowledged bytes
    size_t getBytesSent() const
    {
        return this->stats.totalBytesSent;
    }
    /// Total number of acknowledged payload bytes
    size_t getAckedPayloadBytes() const
    {
        return this->stats.payloadBytesAcked;
    }

private:
    /// size of the stats thread stack, in bytes
    constexpr static const size_t kStatsStackSize = (1024 * 128);
    /// size of the worker thread stack, in bytes
    constexpr static const size_t kWorkerStackSize = (1024 * 256);

private:
    /**
     * @brief Tx queue entry; holds a packet awaiting transmission
    */
    struct pbuf {
        /// Sequence number of the packet to be transmitted
        size_t sequence = 0;
        /// Type of packet
        enum {
            kTypeUnknown,
            kTypeData,
            kTypeFin,
            kTypeSyn
        } type = kTypeUnknown;

        /// timestamp of transmission time
        std::chrono::steady_clock::time_point txTime;
        /// number of times the packet has been transmitted
        size_t numTx = 0;

        /// Size of the payload data
        size_t payloadSz = 0;
        /// Actual payload data for the packet
        char payload[kMaxPacketSize];
    };

    // worker thread context
    struct WorkerCtx {
        WorkerCtx() = delete;
        WorkerCtx(SenderSocket* s, size_t _i) : sock(s) , i(_i) { }

        SenderSocket* sock = nullptr;
        size_t i = 0;
    };

private:
    /// Socket used for communicating
    SOCKET sock = INVALID_SOCKET;
    /// Event signalled when there is data to read from the socket
    HANDLE sockEvent = INVALID_HANDLE_VALUE;

    /// Destination host
    struct sockaddr_storage host = { 0 };
    /// String version of destination host
    std::string hostStr = "";
    /// Window size
    size_t window = 0;

    /// when set, close has been called at least once. no more data should be accepted
    bool isClosing = false;
    /// if set, we've established a connection before
    bool isConnected = false;
    /// time at which the connection was begun to be established
    std::chrono::steady_clock::time_point startTime;
    /// time at which the SYN-ACK was received
    std::chrono::steady_clock::time_point synAckTime;
    /// time the constructor was called
    std::chrono::steady_clock::time_point constructTime;
    /// when the most recent ACK for a data payload packet was received
    std::chrono::steady_clock::time_point dataAckTime;

    /// current retransmission delay
    double rtoDelay = kRetransmissionTimeout;
    /// current sequence number. incremented on every transmission
    DWORD currentSeq = 0;

    /// RTT deviation
    double devRtt = 0;
    /// RTT deviation (previous value)
    double devRttLast = -1;
    /// Estimated RTT
    double estimatedRtt = 0;
    /// Estimated RTT (previous value)
    double estimatedRttLast = -1;

    /// When set, debug logging is on.
    bool debug = false;

    /// handle to the stats thread
    HANDLE statsThread = INVALID_HANDLE_VALUE;
    /// signalled when quit is desired
    HANDLE quitEvent = INVALID_HANDLE_VALUE;

    /// number of worker threads
    constexpr static const size_t kNumWorkers = 1;
    /// handle to the worker threads
    HANDLE workerThread[kNumWorkers] = { INVALID_HANDLE_VALUE };
    /// event signalled by the worker to indicate the connection has become fucked
    HANDLE abortEvent = INVALID_HANDLE_VALUE;
    /// the worker thread signals this every time it passes through its main loop
    HANDLE workerLoopEvent = INVALID_HANDLE_VALUE;

    /// sequence number of the last acknowledged packet
    DWORD lastAckSeq = 0;
    /// number of times an ack was received for the same packet
    size_t lastAckCount = 0;

    /// semaphore indicating queue has empty spots
    HANDLE empty = INVALID_HANDLE_VALUE;
    /// semaphore indicating queue is full
    HANDLE full = INVALID_HANDLE_VALUE;

    /// packets waiting to be transmitted
    std::vector<pbuf> queue;
    /// Sender base value
    size_t senderBase = 0;
    /// Index of packet to send next
    size_t nextToSend = 0;
    /// Last released packet
    size_t lastReleased = 0;

    /// Current stats to print for the stats thread
    struct {
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

        /// number of bytes sent (including headers)
        std::atomic_ulong totalBytesSent = 0;
        /// number of payload bytes acked (total)
        std::atomic_ulong payloadBytesAcked = 0;
        /// last number of bytes acked
        std::atomic_ulong bytesConsumed = 0;

        /// last time stats were printed
        std::chrono::steady_clock::time_point lastPrint;
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

private:
    void setUpWorkerThread();
    void workerThreadMain(WorkerCtx *);

    void workerDrainQueue(bool &);
    void workerTxPacket(pbuf&, bool = true, bool = true);

    void workerReadAck(bool &);
};

#endif