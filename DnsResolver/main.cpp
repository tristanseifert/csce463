// DnsResolver.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#include "pch.h"
#include "DnsResolver.h"
#include "DnsTypes.h"

#include <sstream>
#include <string>
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <random>

#ifdef _WIN32
// this is fucking stupid
#pragma comment(lib, "ws2_32.lib")

/**
 * Performs general initialization of stuff like WinSock
 */
static void WindowsInit()
{
    int err;
    WSAData wsDetails;

    // winsock
    err = WSAStartup(MAKEWORD(2, 2), &wsDetails);

    if (err != 0) {
        std::cerr << "WSAStartup() failed: " << err << std::endl;
        exit(-1);
    }
}
#endif

/**
 * @brief Prints program usage.
*/
static void PrintUsage(const char *name)
{
    std::cout << "usage: " << name << " [record] [server address]" << std::endl;
    std::cout << "\twhere record is a domain name or IPv4 in dotted quad form" << std::endl;
}

/**
 * @brief Prints the DNS response.
*/
static void PrintResponse(const DnsResolver::Response& resp)
{
    // questions section
    std::cout << "  ------------ [questions] ----------" << std::endl;
    for (auto const& question : resp.questions) {
        std::cout << "    " << question << std::endl;
    }
    // answers section
    std::cout << "  ------------ [answers] ------------" << std::endl;
    for (auto const& answer : resp.answers) {
        std::cout << "    " << answer << std::endl;
    }
    // authority section
    std::cout << "  ------------ [authority] ----------" << std::endl;
    for (auto const& answer : resp.authority) {
        std::cout << "    " << answer << std::endl;
    }
    // additional records section
    std::cout << "  ------------ [additional] ---------" << std::endl;
    for (auto const& answer : resp.additional) {
        std::cout << "    " << answer << std::endl;
    }
}

/**
 * @brief Program entry point
 * @return 
*/
int main(int argc, const char** argv)
{
    int err;
    struct sockaddr_storage serverAddr = { 0 };
    struct in_addr reverseAddr = { 0 };
    bool reverse = false;

    // platform specific init
#if _WIN32
    WindowsInit();
#endif

    // validate number of args
    if (argc != 3) {
        PrintUsage(argv[0]);
        return -1;
    }

    // parse the server address
    const std::string serverAddrStr(argv[2]);

    auto server = reinterpret_cast<struct sockaddr_in*>(&serverAddr);
    server->sin_family = AF_INET;
    server->sin_port = htons(53);

    err = inet_pton(AF_INET, serverAddrStr.c_str(), &server->sin_addr);
    if (err != 1) {
        std::cerr << "Failed to parse server address" << std::endl;
        return -1;
    }

    // determine if we need to look up a domain or reverse DNS
    std::string resolveStr(argv[1]);

    err = inet_pton(AF_INET, resolveStr.c_str(), &reverseAddr);

    if (err == -1) {
        perror("inet_pton()");
        return -1;
    }
    reverse = (err == 1);

    if (reverse) {
        std::stringstream reverse;

        for (size_t i = 0; i < 4; i++) {
            uint8_t value = (reverseAddr.S_un.S_addr & (0xFF << (8 * (3 - i)))) >> (8 * (3 - i));
            reverse << (unsigned int)value << ".";
        }

        reverse << "in-addr.arpa";

        resolveStr = reverse.str();
    }

    // calculate a txid hint (random 16-bit value)
    std::random_device dev;
    std::mt19937 random(dev());
    std::uniform_int_distribution<> dist(1, 0xFFFF);

    uint16_t txidHint = dist(random);
    uint16_t type = reverse ? kRecordTypePTR : kRecordTypeA;

    // print lookup details
    std::cout << "Lookup  : " << argv[1] << std::endl;
    std::cout << "Query   : " << resolveStr << ", type " << type << ", TXID 0x" << std::hex 
              << std::setw(4) << txidHint << std::dec << std::endl;
    std::cout << "Server  : " << serverAddrStr << std::endl;
    std::cout << "********************************" << std::endl;

    // do it
    DnsResolver resolver(serverAddr);
    DnsResolver::Response dnsResp;

    try {
        resolver.resolveDomain(resolveStr, dnsResp, type, txidHint);
    } catch (const std::exception& e) {
        std::cerr << "DNS failure: " << e.what() << std::endl;
        return -1;
    }

    if (dnsResp.isSuccess()) {
        std::cout << "  Succeeded with Rcode = " << dnsResp.rcode << std::endl;
        PrintResponse(dnsResp);
    } else {
        std::cout << "  Failed with Rcode = " << dnsResp.rcode << std::endl;
    }
}