#include "pch.h"
#include "DnsResolver.h"
#include "DnsTypes.h"

#include <iostream>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <regex>
#include <random>
#include <chrono>

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
 * @brief Resolves all A records for the given record.
 * @param name Record to resolve
 * @param outResponse Output
*/
void DnsResolver::resolveDomain(const std::string& name, Response& outResponse, uint16_t type, uint16_t txidHint)
{
    int err;
    char response[kMaxPacketLen] = { 0 };
    size_t responseLen, offset;

    // prepare a packet with the fixed header
    char questionPacket[kMaxPacketLen] = { 0 };
    size_t questionLen = sizeof(dns_header_t);

    dns_header_t* questionHeader = reinterpret_cast<dns_header_t*>(&questionPacket);

    questionHeader->flags = htons(kPacketTypeRequest | kRecursionDesired);
    questionHeader->numQuestions = htons(1);

    if (txidHint) {
        questionHeader->txid = htons(txidHint);
    } else {
        std::random_device dev;
        std::mt19937 random(dev());
        std::uniform_int_distribution<> dist(1, 0xFFFF);

        questionHeader->txid = htons(dist(random));
    }

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

    // validate the txid
    auto resHeader = reinterpret_cast<dns_header_t*>(&response);

    if (ntohs(resHeader->txid) != htons(questionHeader->txid)) {
        throw std::runtime_error("txid mismatch");
    }

    // determine if success or not
    resHeader->txid = ntohs(resHeader->txid);
    resHeader->flags = ntohs(resHeader->flags);
    resHeader->numQuestions = ntohs(resHeader->numQuestions);
    resHeader->numAnswers = ntohs(resHeader->numAnswers);
    resHeader->numNameservers = ntohs(resHeader->numNameservers);
    resHeader->numAdditionalRsrc = ntohs(resHeader->numAdditionalRsrc);

    outResponse.packetLen = responseLen;
    memcpy(outResponse.packetData, response, kMaxPacketLen);

    uint16_t rcode = (resHeader->flags & kRcodeMask);
    outResponse.rcode = rcode;
    outResponse.success = (rcode == kRcodeSuccess);

    // parse the question/answer/authority/bonus sections
    offset = this->readQuestions(response, responseLen, outResponse.questions);

    if (offset) {
        offset = this->readAnswers(response, responseLen, &response[offset], outResponse.answers, resHeader->numAnswers);
    }
    if (offset) {
        offset = this->readAnswers(response, responseLen, & response[offset], outResponse.authority, resHeader->numNameservers);
    }
    if (offset) {
        offset = this->readAnswers(response, responseLen, &response[offset], outResponse.additional, resHeader->numAdditionalRsrc);
    }
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
 * @brief Reconstructs a DNS label from the given byte buffer.
 * 
 * This supports compression.
 * 
 * @param packet Address of the base of the packet (the fixed header)
 * @param labelStart First byte of the label
 * @param name Stream into which label components are inserted sequentially
 * @return Number of bytes read, starting at labelStart, for this label
*/
size_t DnsResolver::reconstructLabel(void* packet, const size_t packetLen, void* labelStart, std::stringstream& name, bool *done)
{
    uint8_t* readPtr = reinterpret_cast<uint8_t*>(labelStart);
    size_t bytesRead = 0;

    // read until we get the null terminator
    while (*readPtr != 0) {
        size_t stringBytes = *readPtr;

        // handle checking for end of packet
        if ((readPtr - (uint8_t*)packet) > packetLen) {
            throw std::runtime_error("unexpected end of packet");
        }

        // directly encoded
        if (stringBytes <= 63) {
            std::string chunk((char*)readPtr + 1, stringBytes);
            name << chunk << ".";

            readPtr += 1 + stringBytes;
        }
        // handle compressed strings
        else {
            bool ptrDone = false;

            if ((stringBytes & 0b11000000) != 0xc0) {
                throw std::runtime_error("invalid label length");
            }

            // recurse on down
            uint16_t ptr = ((*readPtr++ << 8) | (*readPtr++)) & 0x3FFF;
            auto nextStart = (reinterpret_cast<uint8_t*>(packet) + ptr);

            if (nextStart == labelStart) {
                throw std::runtime_error("label jump loop");
            }
            if (ptr > packetLen) {
                throw std::runtime_error("jump past end of packet");
            }

            this->reconstructLabel(packet, packetLen, nextStart, name, &ptrDone);

            if (ptrDone) {
                goto gotFullLabel;
            }
        }
    }
    
    // handle trailing zero byte
    if (*readPtr == 0) {
        readPtr++;
    }

gotFullLabel:;
    if (done) {
        *done = true;
    }
    // return number of bytes read
    return readPtr - ((uint8_t*)labelStart);
}



/**
 * @brief Parses the question section out of the given packet. Assumes questions come directly after
 * the header in the given buffer.
 * @param packet 
 * @param questions 
 * @return Offset into buffer for the answers section
*/
size_t DnsResolver::readQuestions(void* packet, const size_t packetLen, std::vector<Question>& questions)
{
    auto header = reinterpret_cast<dns_header_t*>(packet);
    uint8_t* readPtr = reinterpret_cast<uint8_t*>(packet) + sizeof(dns_header_t);

    for (size_t i = 0; i < header->numQuestions; i++) {
        Question q;

        // re-stringify the name
        std::stringstream name;
        readPtr += this->reconstructLabel(packet, packetLen, readPtr, name);

        q.name = name.str();
        q.name.pop_back(); // remove trailing dot

        if ((readPtr - (uint8_t*) packet) > packetLen) {
            throw std::runtime_error("unexpected end of packet");
        }

        // read the type/result code
        auto footer = reinterpret_cast<dns_question_footer_t*>(readPtr);
        
        q.type = ntohs(footer->type);
        q.recordClass = ntohs(footer->reqClass);

        readPtr += sizeof(dns_question_footer_t);

        // prepare for next
        questions.push_back(q);
    }

    // return the number of bytes read
    return readPtr - ((uint8_t*) packet);
}

/**
 * @brief Parses the answers section of the given packet.
 * 
 * @param packet 
 * @param start 
 * @param answers 
 * @return Number of bytes consumed; add this to start ptr to get address of the authority section
*/
size_t DnsResolver::readAnswers(void* packet, const size_t packetLen, void* start, std::vector<Answer> &answers, const size_t count)
{
    auto header = reinterpret_cast<dns_header_t*>(packet);
    uint8_t* readPtr = reinterpret_cast<uint8_t*>(start);

    for (size_t i = 0; i < count; i++) {
        Answer a;

        // re-stringify the name of the answer
        std::stringstream name;
        readPtr += this->reconstructLabel(packet, packetLen, readPtr, name);

        a.name = name.str();
        a.name.pop_back(); // remove trailing dot

        if ((readPtr - (uint8_t*)packet) > packetLen) {
            throw std::runtime_error("unexpected end of packet");
        }

        // read the type/result code
        auto filling = reinterpret_cast<dns_answer_filling_t*>(readPtr);

        a.type = ntohs(filling->type);
        a.payloadClass = ntohs(filling->dataClass);
        a.ttl = ntohl(filling->ttl);

        readPtr += sizeof(dns_answer_filling_t);

        if ((readPtr - (uint8_t*)packet) > packetLen) {
            throw std::runtime_error("unexpected end of packet");
        }

        // lastly, the payload
        std::vector<std::byte> payload;
        size_t payloadLen = ntohs(filling->dataLen);
        payload.resize(payloadLen, std::byte(0));

        if (a.type == kRecordTypePTR || a.type == kRecordTypeCNAME || a.type == kRecordTypeNS) {
            std::stringstream ptrName;
            this->reconstructLabel(packet, packetLen, filling->data, ptrName);

            a.labelValue = ptrName.str();
            a.labelValue.pop_back();
        } else {
            if (((uint8_t *)&filling->data - (uint8_t*)packet) + payloadLen > packetLen) {
                throw std::runtime_error("unexpected end of packet");
            }

            memcpy(payload.data(), filling->data, payloadLen);
        }

        readPtr += payloadLen;
        a.payload = payload;

        // prepare for next
        answers.push_back(a);
    }

    // return the number of bytes read
    return readPtr - ((uint8_t*)packet);
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
    size_t attempt = 0;

    // fixed timeout value
    struct timeval timeout = {0};
    timeout.tv_sec = 10;

    // buffer to read into
    char packet[kMaxPacketLen] = { 0 };
    struct sockaddr_storage respFrom;
    socklen_t respFromLen = sizeof(struct sockaddr_storage);

    while (attempt++ < kMaxRetransmissions) {
        auto startTime = std::chrono::steady_clock::now();
        auto start = std::chrono::duration_cast<std::chrono::milliseconds>(startTime.time_since_epoch()).count();

        std::cout << "Attempt " << attempt << " with " << txBufLen << " bytes...";

        // transmit the packet
        err = sendto(sock, (const char*)txBuf, (int)txBufLen, 0, (struct sockaddr*)&this->toQuery,
            sizeof(struct sockaddr_in));
        if (err == -1) {
            std::cout << "socket error: " << WSAGetLastError() << std::endl;
            throw std::runtime_error("sendto() failed: " + std::to_string(WSAGetLastError()));
        }

        // wait for activity on the socket
        fd_set set;
        FD_ZERO(&set);
        FD_SET(sock, &set);

        err = select(0, &set, nullptr, nullptr, &timeout);
        if (err == -1) {
            throw std::runtime_error("select() failed");
        }
        if (err == 0) {
            auto nowTime = std::chrono::steady_clock::now();
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(nowTime.time_since_epoch()).count();

            std::cout << " timeout in " << (now - start) << " ms" << std::endl;
            continue;
        }

        // receive the packet
        err = recvfrom(sock, packet, kMaxPacketLen, 0, (struct sockaddr*)&respFrom, &respFromLen);
        if (err == -1) {
            std::cout << "socket error: " << WSAGetLastError() << std::endl;
            throw std::runtime_error("recvfrom() failed: " + std::to_string(WSAGetLastError()));
        }
        packetLen = err;

        // print time taken to receive this packet, then process it
        auto nowTime = std::chrono::steady_clock::now();
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(nowTime.time_since_epoch()).count();

        std::cout << " response in " << (now - start) << " ms with " << packetLen << " bytes" << std::endl;
        goto gotPacket;
    }

    // failed to receive packet
    throw std::runtime_error("failed to receive DNS response");

 gotPacket:;
    // TODO: make sure packet came from the same host that we sent data to

    // copy out
    memcpy(_rxBuf, packet, min(packetLen, _rxBufLen));
    return packetLen;
}



/**
 * @brief Prints a string representation of a question record.
*/
std::ostream& operator<<(std::ostream& output, const DnsResolver::Question& question)
{
    output << question.name << " type " << (unsigned int) question.type 
           << " class " << (unsigned int) question.recordClass;
    return output;
}
 
/**
 * @brief Prints a string representation of an answer record.
*/
std::ostream& operator<<(std::ostream& output, const DnsResolver::Answer& answer) {
    std::string typeStr = std::to_string(answer.type);
    std::string valueStr = "(unknown value)";

    switch (answer.type) {
    case kRecordTypeA: {
        typeStr = "A";

        std::stringstream addrStream;
        addrStream << (int)answer.payload[0] << ".";
        addrStream << (int)answer.payload[1] << ".";
        addrStream << (int)answer.payload[2] << ".";
        addrStream << (int)answer.payload[3];

        valueStr = addrStream.str();

        break;
    }
    case kRecordTypeNS:
        typeStr = "NS";
        valueStr = answer.labelValue;
        break;
    case kRecordTypeCNAME:
        typeStr = "CNAME";
        valueStr = answer.labelValue;
        break;
    case kRecordTypePTR:
        typeStr = "PTR";
        valueStr = answer.labelValue;
        break;
    case kRecordTypeMX:
        typeStr = "MX";
        break;
    }

    output  << answer.name << " " << typeStr << " " << valueStr
              << " TTL = " << (unsigned int) answer.ttl;

    return output;
}