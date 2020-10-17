#include "pch.h"
#include "SenderSocket.h"
#include "PacketTypes.h"

#include <string>
#include <iostream>

#ifdef _WIN32
// what the hell are they smoking at microsoft to come up with this bullshit
#pragma comment(lib, "ws2_32.lib")

/**
 * Performs general initialization of stuff like WinSock
 */
static void WinbowlsInit()
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
 * @brief Program entry point
 * @return 
*/
int main(int argc, const char **argv)
{
    size_t power, senderWindow, bufSize;
    float rtt, loss[2], speed;

    // platform init
#ifdef _WIN32
    WinbowlsInit();
#endif

	// read the command line args in
	if (argc != 8) {
    printUsage:;
        std::cerr << "usage: " << argv[0] << " [server address] [log2 buffer size] [window size] "
                    << std::endl
                    << "[RTT] [forward loss] [reverse loss] [bottleneck link speed]"
                    << std::endl;
        return -1;
	}

    // get server address, and the buffer/window sizes
    std::string serverAddr(argv[1]);

    power = std::atoi(argv[2]);
    if (power <= 0 || power > 36) {
        std::cerr << "invalid buffer size" << std::endl;
        goto printUsage;
    }
    bufSize = (size_t) std::pow(2, power);

    senderWindow = std::atoi(argv[3]);
    if (senderWindow <= 0) {
        std::cerr << "invalid sender window size" << std::endl;
        goto printUsage;
    }

    // floating point values (aka everything else)
    rtt = std::stof(argv[4]);
    if (rtt <= 0 || rtt >= 30) {
        std::cerr << "invalid RTT" << std::endl;
        goto printUsage;
    }

    loss[kForwardDirection] = std::stof(argv[5]);
    if (loss[kForwardDirection] < 0 || loss[kForwardDirection] >= 1) {
        std::cerr << "invalid forward loss" << std::endl;
        goto printUsage;
    }

    loss[kReturnDirection] = std::stof(argv[6]);
    if (loss[kReturnDirection] < 0 || loss[kReturnDirection] >= 1) {
        std::cerr << "invalid return loss" << std::endl;
        goto printUsage;
    }

    speed = 1e6f * std::stof(argv[7]);
    if (speed <= 0 || speed > 10000 * 10e6) {
        std::cerr << "invalid bottleneck bandwidth" << std::endl;
        goto printUsage;
    }

    // everything appears to be in order here
    SenderSocket sock;
    try {
        // connect socket
        sock.open(serverAddr, SenderSocket::kPortNumber, senderWindow, rtt, speed, loss);

        // TODO: send loop

        // done
        sock.close();
    } catch(SenderSocket::SocketError &e) {
        std::cerr << "Socket error: " << e.what() << std::endl;
        return -1;
    }
}