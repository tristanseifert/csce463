/**
* This file contains various defines for working with DNS packets.
* 
* The overall structure of the DNS packet is simple; each packet starts off with a single fixed
* header (dns_header_t). Following this header are the question, answer, authority and additional
* record sections, depending on the counts in the header.
 */
#ifndef DNSTYPES_H
#define DNSTYPES_H

#include <stdint.h>

// some shenanigans to pack structs on winbowls
#ifdef WIN32
#define PACKED 
#pragma pack(push, 1)
#else
#define PACKED __attribute__((packed))
#endif

/**
 * @brief Flags for the DNS header
*/
enum dns_flags {
    /// Packet is a request
    kPacketTypeRequest  = (0 << 15),
    /// Packet is a response
    kPacketTypeResponse = (1 << 15),

    /// Name server is authoratative for domain
    kAnswerAuthoratative = (1 << 10),
    /// Response was truncated
    kTruncation = (1 << 9),
    /// Recursion desired
    kRecursionDesired = (1 << 8),
    /// Indicate whether recursion is available
    kRecursionAvailable = (1 << 7),

    /// Mask for the rcode portion
    kRcodeMask = (0x000F),
    /// Mask for the opcode portion
    kPacketOpcodeMask = (0x7800),
};

/**
 * @brief rcode field values
*/
typedef enum dns_rcode {
    kRcodeSuccess = 0,
    kRcodeInvalidFormat = 1,
    kRcodeServerError = 2,
    kRcodeNameError = 3,
    kRcodeNotImplemented = 4,
    kRcodeRefused = 5,
} dns_rcode_t;

/**
 * @brief Fixed DNS header. This is at the start of every DNS packet.
 * 
 * Note that all multi-byte quantitities are in network byte order.
*/
typedef struct PACKED dns_header {
	/// Transaction ID
    uint16_t txid;

	/// Flags: see the dns_flags_t enum (this is not byteswapped)
    uint16_t flags;

	/// Number of questions
    uint16_t numQuestions;
    /// Number of answers
    uint16_t numAnswers;
    /// Number of NS (nameserver) records in the authority section
    uint16_t numNameservers;
    /// Number of resource records in the additional records section
    uint16_t numAdditionalRsrc;
} dns_header_t;

/**
 * @brief Question record types
*/
typedef enum dns_question_type : uint16_t {
    /// A record
    kRecordTypeA = 1,
    /// Nameserver records
    kRecordTypeNS = 2,
    /// CNAME records
    kRecordTypeCNAME = 5,
    /// IP to hostname
    kRecordTypePTR = 12,
    /// mail servers
    kRecordTypeMX = 15,
    /// all records
    kRecordTypeAny = 255,
} dns_question_type_t;

/**
 * @brief Footer for a question
*/
typedef struct PACKED dns_question_footer {
    /// Question type
    uint16_t type;
    /// Desired class
    uint16_t reqClass;
} dns_question_footer_t;


#ifdef WIN32
#pragma pack(pop)
#endif

#endif