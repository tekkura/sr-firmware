#include "pico/types.h"
#include "serial_comm_manager.h"
#include "pico/stdio.h"
#include <string.h>
#include <stdio.h>
#include "robot.h"
#include "rp2040_log.h"
#include "crc.h"
#include "version.h"

static RP2040_STATE rp2040_state_;
static uint8_t rx_buffer[MAX_PAYLOAD_SIZE];

void handle_packet(uint8_t cmd, const uint8_t *payload, uint16_t payload_len);

void serial_comm_manager_init(RP2040_STATE* rp2040_state){
    rp2040_state_ = *rp2040_state;
}

/**
 * Sends a framed packet using a zero-copy streaming approach.
 * Format: [SOF][LEN_L][LEN_H][CMD][PAYLOAD...][CRC_L][CRC_H]
 * CRC is calculated over [LEN_L, LEN_H, CMD, PAYLOAD]
 */
void send_framed_packet(uint8_t cmd, const uint8_t* data, uint16_t len) {
    uint16_t total_payload_len = len + 1; // cmd + data
    uint8_t len_bytes[2];
    len_bytes[0] = total_payload_len & 0xFF;
    len_bytes[1] = (total_payload_len >> 8) & 0xFF;

    // 1. Calculate CRC
    uint16_t crc = crc16_ccitt(len_bytes, 2, 0xFFFF);
    crc = crc16_ccitt(&cmd, 1, crc);
    if (len > 0 && data != NULL) {
        crc = crc16_ccitt(data, len, crc);
    }

    // 2. Transmission
    putchar(START_MARKER);
    putchar(len_bytes[0]);
    putchar(len_bytes[1]);
    putchar(cmd);
    if (len > 0 && data != NULL) {
        for (uint16_t i = 0; i < len; i++) {
            putchar(data[i]);
        }
    }
    putchar(crc & 0xFF);
    putchar((crc >> 8) & 0xFF);
}

/**
 * Sends a log packet using a zero-copy streaming approach.
 * Format: [SOF][LEN_L][LEN_H][CMD][LOG...][CRC_L][CRC_H]
 * CRC is calculated over [LEN_L, LEN_H, CMD, LOG]
 */
void send_log_packet() {
    rp2040_log_acquire_lock();
    uint16_t total_payload_len = rp2040_get_byte_count() + 1; // cmd + data
    uint8_t len_bytes[2];
    len_bytes[0] = total_payload_len & 0xFF;
    len_bytes[1] = (total_payload_len >> 8) & 0xFF;

//    // 1. Calculate CRC
    uint16_t crc = crc16_ccitt(len_bytes, 2, 0xFFFF);
    uint8_t cmd = GET_LOG;
    crc = crc16_ccitt(&cmd, 1, crc);
    crc = rp2040_get_crc(crc);

    // 2. Transmission
    putchar(START_MARKER);
    putchar(len_bytes[0]);
    putchar(len_bytes[1]);
    putchar(GET_LOG);
    rp2040_log_flush();
    putchar(crc & 0xFF);
    putchar((crc >> 8) & 0xFF);

    rp2040_log_release_lock();
}

/**
 * Reads a single byte from UART with timeout.
 */
static inline int read_byte() {
    return getchar_timeout_us(100); // 100us timeout
}

/**
 * Receives a framed block from the host.
 */
void get_block() {
    int c = read_byte();
    if (c == PICO_ERROR_TIMEOUT || c != START_MARKER) return;

    // Read Length (2 bytes)
    int l1 = read_byte();
    if (l1 == PICO_ERROR_TIMEOUT) {
        send_framed_packet(NACK, NULL, 0);
        return;
    }

    int l2 = read_byte();
    if (l2 == PICO_ERROR_TIMEOUT) {
        send_framed_packet(NACK, NULL, 0);
        return;
    }

    uint16_t total_payload_len = (uint16_t)l1 | ((uint16_t)l2 << 8);
    if (total_payload_len == 0 || total_payload_len > MAX_PAYLOAD_SIZE) {
        send_framed_packet(NACK, NULL, 0);
        return;
    }

    // Read Command + Payload
    // Note: total_payload_len includes 1 byte for command ID
    for (uint16_t i = 0; i < total_payload_len; i++) {
        int b = read_byte();
        if (b == PICO_ERROR_TIMEOUT) {
            send_framed_packet(NACK, NULL, 0);
            return;
        }

        rx_buffer[i] = (uint8_t)b;
    }

    // Read CRC (2 bytes)
    int c1 = read_byte();
    if (c1 == PICO_ERROR_TIMEOUT) {
        send_framed_packet(NACK, NULL, 0);
        return;
    }

    int c2 = read_byte();
    if (c2 == PICO_ERROR_TIMEOUT) {
        send_framed_packet(NACK, NULL, 0);
        return;
    }

    uint16_t received_crc = (uint16_t)c1 | ((uint16_t)c2 << 8);

    // Verify CRC
    uint8_t len_bytes[2] = {(uint8_t)l1, (uint8_t)l2};
    uint16_t calculated_crc = crc16_ccitt(len_bytes, 2, 0xFFFF);
    calculated_crc = crc16_ccitt(rx_buffer, total_payload_len, calculated_crc);

    if (calculated_crc != received_crc) {
        // Send NACK on integrity failure
        send_framed_packet(NACK, NULL, 0);
        return;
    }

    // Process Packet
    uint8_t cmd = rx_buffer[0];
    const uint8_t *payload = (total_payload_len > 1) ? &rx_buffer[1] : NULL;
    uint16_t payload_len = total_payload_len - 1;

    handle_packet(cmd, payload, payload_len);
}

void handle_packet(uint8_t cmd, const uint8_t *payload, uint16_t payload_len) {
    switch (cmd) {
        case GET_LOG: {
            send_log_packet();
            break;
        }
        case SET_MOTOR_LEVEL: {
            if (payload_len == 2) {
                uint8_t left = payload[0];
                uint8_t right = payload[1];

                // Clear the state struct to avoid leaking stale bytes from prior commands
                memset(&rp2040_state_, 0, sizeof(rp2040_state_));

                rp2040_state_.MotorsState.ControlValues.left = left;
                rp2040_state_.MotorsState.ControlValues.right = right;
                set_motor_levels(&rp2040_state_);

                get_state(&rp2040_state_);
                send_framed_packet(SET_MOTOR_LEVEL, (uint8_t*)&rp2040_state_, sizeof(rp2040_state_));
            } else {
                send_framed_packet(NACK, NULL, 0);
            }
            break;
        }
        case GET_STATE: {
            // Clear the state struct to avoid leaking stale bytes from prior commands
            memset(&rp2040_state_, 0, sizeof(rp2040_state_));

            get_state(&rp2040_state_);
            send_framed_packet(GET_STATE, (uint8_t*)&rp2040_state_, sizeof(rp2040_state_));
            break;
        }
        case GET_VERSION: {
            VERSION version;
            version.version_major = FW_VERSION_MAJOR;
            version.version_minor = FW_VERSION_MINOR;
            version.version_patch = FW_VERSION_PATCH;

            send_framed_packet(GET_VERSION, (uint8_t*)&version, sizeof(version));
            break;
        }
        case RESET_STATE:
            send_framed_packet(ACK, NULL, 0);
            break;
        default:
            send_framed_packet(NACK, NULL, 0);
            break;
    }
}
