#include "pch.h"
#include "DnsResolver.h"
#include "DnsTypes.h"

#include <cstring>
#include <sstream>
#include <algorithm>
#include <regex>
#include <random>

/**
 * @brief Sets the given address as the nameserver we're querying.
*/
void DnsResolver::setNameserverAddr(const struct sockaddr_storage& ipAddr, const unsigned int port)
{
    switch (ipAddr.ss_family) {
    case AF_INET:
        memcpy(&this->toQuery, &ipAddr, sizeof(struct sockaddr_in));

        struct sockaddr_in* addr = (struct sockaddr_in*)&this->toQuery;
        addr->sin_port = htons(port);
        break;
    }
}

/**
 * @brief Resolves the PTR record for the given address.
 * @param addr 
 * @param outResponse 
*/
void DnsResolver::resolveReverse(const in_addr& addr, Response& outResponse)
{
    // TODO: yeet
    
    // fuck me
}

/**
 * @brief Resolves all A records for the given record.
 * @param name Record to resolve
 * @param outResponse Output
*/
void DnsResolver::resolveDomain(const std::string& name, Response& outResponse, uint16_t type)
{
    int err;
    char response[kMaxPacketLen] = { 0 };
    size_t responseLen;

    std::random_device dev;
    std::mt19937 random(dev());
    std::uniform_int_distribution<> dist(0, 0xFFFF);

    // prepare a packet with the fixed header
    char questionPacket[kMaxPacketLen] = { 0 };
    size_t questionLen = sizeof(dns_header_t);

    dns_header_t* questionHeader = reinterpret_cast<dns_header_t*>(&questionPacket);

    questionHeader->flags = htons(kPacketTypeRequest | kRecursionDesired);
    questionHeader->numQuestions = htons(1);
    questionHeader->txid = htons(dist(random));

    questionLen += this->putQuestion(&questionPacket[sizeof(dns_header_t)], 
        (questionLen - sizeof(dns_header_t)), name, type, 0x0001);

    // open the UDP socket, then send txn
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1) {
        throw std::runtime_error("socket() failed");
    }

    struct sockaddr_in local = { 0 };
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = htons(0);

    err = bind(sock, (struct sockaddr*)&local, sizeof(local));
    if (err == -1) {
        throw std::runtime_error("bind() failed: " + std::to_string(WSAGetLastError()));
    }

    responseLen = this->singlePacketTxn(sock, questionPacket, questionLen, response, kMaxPacketLen);

    closesocket(sock);

    // inspect result
    outResponse.packetLen = responseLen;
    memcpy(outResponse.packetData, response, kMaxPacketLen);
}

/**
 * @brief Writes a question record to the given pointer. This record contains the given label
 * (split into its component parts) and the provided type and result class.
 * @param writePtr Where in memory to write the question
 * @param labels String to parse; split on periods
 * @param type Query type
 * @param resultClass Desired result class
 * @return The number of bytes written for the question record
*/
size_t DnsResolver::putQuestion(void* _writePtr, size_t _writePtrLen, const std::string& label, uint16_t type, uint16_t resultClass)
{
    uint8_t* writePtr = reinterpret_cast<uint8_t*>(_writePtr);

    // split the string labels and write them into the query
    std::vector<std::string> pieces;
    this->splitLabel(label, pieces);

    if (pieces.empty()) { 
        pieces.push_back(label);
    }

    // write each of the pieces
    for (auto const& label : pieces) {
        // the length (may not be more than 64 bytes)
        if (label.size() >= 64) {
            throw std::runtime_error("label too long: '" + label + "'");
        }

        *writePtr++ = (uint8_t)label.size() & 0x3F;

        // copy all bytes
        memcpy(writePtr, label.c_str(), label.size());
        writePtr += label.size();
    }

    // the apex (terminating) null label
    *writePtr++ = '\0';

    // footer
    auto footer = reinterpret_cast<dns_question_footer_t*>(writePtr);

    footer->type = htons(type);
    footer->reqClass = htons(resultClass);

    writePtr += sizeof(dns_question_footer_t);

    // return the bytes written
    return writePtr - ((uint8_t *) _writePtr);
}

/**
 * @brief Splits a domain name by period into components
 * @param label 
 * @param pieces 
*/
void DnsResolver::splitLabel(const std::string& label, std::vector<std::string>& pieces)
{
    std::string piece;
    std::stringstream stream(label);

    while (std::getline(stream, piece, '.')) {
        pieces.push_back(piece);
    }
}

/**
 * @brief Runs a single packet UDP transaction against the DNS server.
 * @param sock Socket to use in sending/receiving query
 * @param txBuf Packet data to transmit
 * @param txBufLen Length of packet data to transmit
 * @param rxBuf Buffer to receive response packet
 * @param rxBufLen Size of receive buffer; if packet is larger, it is truncated
 * @return Number of bytes received
*/
size_t DnsResolver::singlePacketTxn(SOCKET sock, const void* txBuf, const size_t txBufLen, void* _rxBuf, const size_t _rxBufLen)
{
    int err = 0;
    size_t packetLen = 0;

    // buffer to read into
    char packet[kMaxPacketLen] = {0};
    struct sockaddr_storage respFrom;
    socklen_t respFromLen = sizeof(struct sockaddr_storage);

    // transmit the packet
    err = sendto(sock, (const char*)txBuf, txBufLen, 0, (struct sockaddr*)&this->toQuery, 
        sizeof(struct sockaddr_in));
    if (err == -1) {
        throw std::runtime_error("sendto() failed: " + std::to_string(WSAGetLastError()));
    }

    // receive the packet
    // TODO: handle timeouts/retransmissions
    err = recvfrom(sock, packet, kMaxPacketLen, 0, (struct sockaddr *) &respFrom, &respFromLen);
    if (err == -1) {
        throw std::runtime_error("recvfrom() failed: " + std::to_string(WSAGetLastError()));
    }
    packetLen = err;

    // copy out
    memcpy(_rxBuf, packet, min(packetLen, _rxBufLen));
    return packetLen;
}
