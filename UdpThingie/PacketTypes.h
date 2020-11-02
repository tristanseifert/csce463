// pragma once is the dumbest shit i have ever seen
#ifndef PACKETTYPES_H
#define PACKETTYPES_H

#include "pch.h"

// pack all structs (again in the most moronic michaelsoft way)
#pragma pack(push, 1)

/// Magic value to be shoved into the flags field
constexpr static const DWORD /* ugh */ kFlagsMagic = 0x8311AA;

/**
 * @brief A set of flags each packet header can have
*/
struct Flags {
    /// must be 0
    DWORD reserved : 5;
    DWORD syn : 1;
    DWORD ack : 1;
    DWORD fin : 1;
    /// must be set to the magic value
    DWORD magic : 24;
};
/**
 * @brief Header for all transmitted packets
*/
struct SenderPacketHeader {
    Flags flags;
    /// Sequence number
    DWORD seq;
};
/**
 * @brief Header for all received packets
*/
struct ReceiverPacketHeader {
    Flags flags;
    /// Receiver window size (in packets)
    DWORD receiveWindow;
    /// Next expected sequence
    DWORD ackSeq;
};

/**
 * @brief Properties defining the link to be emulated by the server.
*/
struct LinkProperties {
    /// round trip time (in seconds)
    float RTT;
    /// bottleneck bandwidth (in bps)
    float speed;
    /// probability of packet loss in each direction
    float pLoss[2];
    /// size of the router buffers, in packets
    DWORD bufferSize;
};
/**
 * @brief Indices for the `pLoss` array in the link properties struct.
*/
enum PathLossDirection {
    /// Path loss in the forward direction
    kForwardDirection = 0,
    /// Path loss in the reverse direction
    kReturnDirection = 1,
};

/**
 * @brief Packet transmitted to set up a connection
*/
struct SenderSynPacket {
    /// packet header
    SenderPacketHeader header;
    /// link properties to emulate
    LinkProperties lp;
};

/**
 * @brief Packet containing payload data
*/
struct SenderDataPacket {
    /// packet header
    SenderPacketHeader header;
    /// payload
    char data[];
};

// this is stupid
#pragma pack(pop)

#endif