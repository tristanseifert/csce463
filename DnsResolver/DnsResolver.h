#ifndef DNSRESOLVER_H
#define DNSRESOLVER_H

#include <cstddef>
#include <string>
#include <vector>
#include <tuple>

/**
 * @brief Implements the actual DNS resolution.
*/
class DnsResolver {
private:
    /// Maximum DNS packet size
    constexpr static const size_t kMaxPacketLen = 512;

public:
    /// question record
    struct Question {
        /// Name
        std::string name;
        /// Type
        uint16_t type;
        /// record class (1 = INET)
        uint16_t recordClass;

        friend std::ostream& operator<<(std::ostream& output, const Question&); 
    };

    /// answer record
    struct Answer {
        friend class DnsResolver;

        /// Name
        std::string name;
        /// Type
        uint16_t type;
        /// Type of payload
        uint16_t payloadClass;
        /// Time-to-live (seconds)
        unsigned int ttl;

        /// Answer payload (raw)
        std::vector<std::byte> payload;
        
        friend std::ostream& operator<<(std::ostream& output, const Answer&);

    private:
        /// If a PTR record, this is the hostname container within
        std::string ptrValue;
    };

    /// DNS query response
    struct Response {
        friend class DnsResolver;

    public:
        bool isSuccess() const
        {
            return this->success;
        }
        
        /// whether the response was successful or not
        bool success = false;
        /// response code
        int rcode = -1;

        /// Questions asked
        std::vector<Question> questions;
        /// Received answer records
        std::vector<Answer> answers;
        /// Received authority records
        std::vector<Answer> authority;
        /// Bonus records
        std::vector<Answer> additional;

    private:
        /// length of the packet
        size_t packetLen;
        /// Full received packet
        char packetData[DnsResolver::kMaxPacketLen];
    };

public:
    DnsResolver() = delete;
    DnsResolver(const struct sockaddr_storage& addr)
    {
        this->setNameserverAddr(addr);
    }
    virtual ~DnsResolver() {};

public:
    void resolveDomain(const std::string& name, Response& outResponse, uint16_t type = 1, uint16_t txidHint = 0);

    void setNameserverAddr(const struct sockaddr_storage& ipAddr, const unsigned int port = 53);

private:
    size_t putQuestion(void *writePtr, size_t writePtrLeft, const std::string& labels, uint16_t type, uint16_t resultClass);

    void splitLabel(const std::string& label, std::vector<std::string>& pieces);
    size_t reconstructLabel(void* packet, void* labelStart, std::stringstream& name, bool *done = nullptr);

    size_t readQuestions(void* packet, std::vector<Question> &questions);
    size_t readAnswers(void* packet, void* start, std::vector<Answer> &answers);

    size_t singlePacketTxn(SOCKET sock, const void* txBuf, const size_t txBufLen, void* rxBuf, const size_t rxBufLen);

private:
    /// IP address of the server to query
    struct sockaddr_storage toQuery = {0};
};

#endif