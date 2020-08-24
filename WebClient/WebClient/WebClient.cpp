// WebClient.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#include "pch.h"

#include <iostream>

#include "URL.h"

// what kind of idiot decided this is how you link against libraries
#pragma comment(lib, "ws2_32.lib")

/**
 * Performs general initialization of stuff like WinSock (more like WinSuck lmao)
 */
static void CommonInit()
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

/**
 * Program entry point
 */
int main(int argc, const char** argv)
{
    // perform initialization
    CommonInit();

    // get URL from arguments
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " [url]" << std::endl;
        return -1;
    }

    try {
        auto url = URL(argv[1]);
    } catch (std::exception &e) {
        std::cerr << "Failed to parse url: " << e.what() << std::endl;
        return -1;
    }

    // load the URL
    std::cout << "Hello World!\n";

    // cleanup
    WSACleanup();
}