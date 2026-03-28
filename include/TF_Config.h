#ifndef TF_CONFIG_H
#define TF_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "rp2040_log.h"

// --- TinyFrame Configuration ---

// Use CRC16 for better reliability over USB/Serial
#define TF_CKSUM_TYPE TF_CKSUM_CRC16

// Field sizes (in bytes)
#define TF_ID_BYTES 1
#define TF_LEN_BYTES 2
#define TF_TYPE_BYTES 1

// Buffer sizes
#define TF_MAX_PAYLOAD_RX 256
// Reduced from 1024 to 64 to save SRAM.
// Large packets (logs) are streamed via Multipart and don't need a large send buffer.
#define TF_SENDBUF_LEN 64

// Max number of listeners (Optimized to exact requirements)
#define TF_MAX_ID_LST 1      // Minimum allocation
#define TF_MAX_TYPE_LST 4    // Exactly 4 for GET_LOG, SET_MOTOR, GET_STATE, RESET_STATE
#define TF_MAX_GEN_LST 1     // Exactly 1 for the NACK catch-all

// Data types for counters and ticks
typedef uint32_t TF_TICKS;
typedef uint8_t TF_COUNT;

// Parser settings
#define TF_USE_SOF_BYTE 1
#define TF_SOF_BYTE 0x01
#define TF_PARSER_TIMEOUT_TICKS 100

// Error reporting
#define TF_Error(format, ...) rp2040_log("[TF] ERROR: " format "\n", ##__VA_ARGS__)

// Thread safety
#define TF_USE_MUTEX 0

#endif // TF_CONFIG_H
