#include "pch.h"
#include "SenderSocket.h"
#include "PacketTypes.h"
#include "Checksum.h"

#include <string>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <chrono>

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
    size_t power, senderWindow, bufSize, bufSizeBytes;
    float rtt, loss[2], speed;
    Checksum cs;

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
    bufSizeBytes = bufSize * sizeof(DWORD);

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

    // print the info
    std::cout << "Main:\tsender W = " << senderWindow << ", RTT " << rtt << " sec, loss "
              << loss[0] << " / " << loss[1] << ", link " << (speed / 1e6) << " Mbps" << std::endl;

    // allocate the buffer
    std::cout << "Main:\tinitializing DWORD array with 2^" << power << " elements...";
    auto bufFillStart = std::chrono::steady_clock::now();

    DWORD* buf = new DWORD[bufSize];
    for (size_t i = 0; i < bufSize; i++) {
        buf[i] = (DWORD) i;
    }

    auto bufFillEnd = std::chrono::steady_clock::now();
    std::cout << " done in "
              << std::chrono::duration_cast<std::chrono::milliseconds>(bufFillEnd - bufFillStart).count()
              << " ms" << std::endl;

    std::cout << "Main:\tcalculating expected CRC32... ";
    uint32_t check = cs.crc32(buf, bufSizeBytes);
    bufFillStart = std::chrono::steady_clock::now();

    std::cout << "$" << std::setw(8) << std::hex << check << "; done in " << std::dec
              << std::chrono::duration_cast<std::chrono::milliseconds>(bufFillStart - bufFillEnd).count()
              << " ms" << std::endl;

    // everything appears to be in order here
    SenderSocket sock;
    try {
        // connect socket
        auto connectStart = std::chrono::steady_clock::now();
        sock.open(serverAddr, SenderSocket::kPortNumber, senderWindow, rtt, speed, loss);
        auto connectEnd = std::chrono::steady_clock::now();

        std::cout << "Main:\tConnected to " << serverAddr << " in "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(connectEnd - connectStart).count() / 1000.f
                  << " sec. Packet size is " << SenderSocket::kMaxPacketSize << " bytes" << std::endl;

        // repeatedly send
        auto sendStartTime = std::chrono::steady_clock::now();

        size_t off = 0;
        while (off < bufSizeBytes) {
            size_t numBytes = min(bufSizeBytes - off, SenderSocket::kMaxPacketSize - sizeof(SenderPacketHeader));
            sock.send(((uint8_t *) buf) + off, numBytes);
            off += numBytes;
        }
        auto sendEnd = std::chrono::steady_clock::now();

        // done
        sock.close();
        auto now = std::chrono::steady_clock::now();

        double transferLenSec = std::chrono::duration_cast<std::chrono::milliseconds>(sock.getDataAckTime() - sendStartTime).count() / 1000.f;
        double rateSecs = std::chrono::duration_cast<std::chrono::milliseconds>(now - sock.getSynAckTime()).count() / 1000.f;

        double rate = (((double)sock.getBytesSent() * 8) / rateSecs) / 1000.f;
        std::cout << "Main:\tTransfer finished in " << transferLenSec << " sec" 
                  << ", " << rate << " Kbps, checksum $" << std::hex << std::setw(8) 
                  << std::setfill('0') << check <<  std::endl;

        double idealRate = ((double) SenderSocket::kMaxPacketSize * 8 * senderWindow) / sock.getEstimatedRtt();
        std::cout << "Main:\testRtt " << sock.getEstimatedRtt() << ", ideal rate "
                  << idealRate / 1000.f << " Kbps" << std::endl;
    } catch(SenderSocket::SocketError &e) {
        std::cerr << "Socket error " << e.getType() << ": " << e.what() << std::endl;
        return -1;
    }

    // clean up
    delete[] buf;
    return 0;
}