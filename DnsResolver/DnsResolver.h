#ifndef DNSRESOLVER_H
#define DNSRESOLVER_H

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
    /// DNS query response
    struct Response {
        friend class DnsResolver;

    public:
        bool isSuccess() const
        {
            return this->success;
        }
        
    private:
        /// whether the response was successful or not
        bool success = false;

        /// length of the packet
        size_t packetLen;
        /// Full received packet
        char packetData[DnsResolver::kMaxPacketLen];

        /// Questions asked
        std::vector<int> questions;
        /// Received answer records
        std::vector<int> answers;
        /// Received authority records
        std::vector<int> authority;
        /// Bonus records
        std::vector<int> additional;
    };

public:
    DnsResolver() = delete;
    DnsResolver(const struct sockaddr_storage& addr)
    {
        this->setNameserverAddr(addr);
    }
    virtual ~DnsResolver() {};

public:
    void resolveDomain(const std::string& name, Response& outResponse, uint16_t type = 1);
    void resolveReverse(const struct in_addr& addr, Response& outResponse);

    void setNameserverAddr(const std::string& ipAddrString);
    void setNameserverAddr(const struct sockaddr_storage& ipAddr, const unsigned int port = 53);

private:
    size_t putQuestion(void *writePtr, size_t writePtrLeft, const std::string& labels, uint16_t type, uint16_t resultClass);
    void splitLabel(const std::string& label, std::vector<std::string>& pieces);

    size_t singlePacketTxn(SOCKET sock, const void* txBuf, const size_t txBufLen, void* rxBuf, const size_t rxBufLen);

private:
    /// IP address of the server to query
    struct sockaddr_storage toQuery = {0};
};

#endif