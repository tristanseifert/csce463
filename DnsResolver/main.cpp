// DnsResolver.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#include "pch.h"
#include "DnsResolver.h"

#include <string>
#include <stdexcept>
#include <iostream>

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
    const std::string resolveStr(argv[1]);

    err = inet_pton(AF_INET, resolveStr.c_str(), &reverseAddr);

    if (err == -1) {
        perror("inet_pton()");
        return -1;
    }
    reverse = (err == 1);

    // do it
    DnsResolver resolver(serverAddr);
    DnsResolver::Response dnsResp;

    try {
        if (reverse) {
            resolver.resolveReverse(reverseAddr, dnsResp);
        } else {
            resolver.resolveDomain(resolveStr, dnsResp);
        }
    } catch (const std::exception& e) {
        std::cerr << "DNS failure: " << e.what() << std::endl;
        return -1;
    }
}