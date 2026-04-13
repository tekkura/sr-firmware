#include "pico/types.h"
#include "serial_comm_manager.h"
#include "pico/stdio.h"
#include <string.h>
#include <stdio.h>
#include "robot.h"
#include "rp2040_log.h"
#include "tusb.h"
#include "version.h"

static IncomingPacketFromAndroid incoming_packet_from_android;
static OutgoingPacketToAndroid outgoing_packet_to_android;
static OutgoingLogPacketToAndroid outgoing_log_packet_to_android;
static OutgoingVersionPacketAndroid outgoing_version_packet_to_android;

void handle_packet(IncomingPacketFromAndroid *packet);
static RP2040_STATE rp2040_state_;

/**
 * Optimized CDC Write
 * Bypasses the pico_stdio layer to write directly to the TinyUSB hardware FIFO.
 * Flushes immediately to trigger transmission.
 * Returns true if all bytes were written, false on timeout.
 */
static bool cdc_write_optimized(const void* buf, uint32_t len, bool flush) {
    if (!tud_cdc_n_connected(0)) return false;

    const uint8_t* p = (const uint8_t*) buf;
    uint32_t remaining = len;
    uint32_t retries = 0;

    // DETERMINISTIC PATH: If the whole packet fits in the FIFO, write it all at once.
    // This avoids the retry/sleep logic for standard state packets.
    uint32_t available = tud_cdc_n_write_available(0);
    if (available >= len) {
        tud_cdc_n_write(0, buf, len);
        if (flush) tud_cdc_n_write_flush(0);
        return true;
    }

    // RETRY PATH: Only used if FIFO is full (e.g. during heavy logging)
    while (remaining > 0 && tud_cdc_n_connected(0)) {
        uint32_t written = tud_cdc_n_write(0, p, remaining);
        if (written > 0) {
            p += written;
            remaining -= written;
            retries = 0;
        } else {
            if (++retries > WRITE_MAX_RETRIES) {
                return false;
            }

#if CFG_TUSB_MCU == OPT_MCU_RP2040
            tud_task();
#endif
            if (retries == WRITE_FLUSH_TRIGGER) {
                // MECHANICAL FLUSH: If we are stuck, force a packet out to make room.
                // We only do this after a couple of retries to allow for natural bus drainage.
                tud_cdc_n_write_flush(0);
            }

            // Tight spin before sleeping to keep jitter low
            if (retries > 1)
                sleep_us(WRITE_RETRY_DELAY);
        }
    }

    if (flush) tud_cdc_n_write_flush(0);

    return remaining == 0;
}

#define cdc_write(buf, len) cdc_write_optimized(buf, len, true)
#define cdc_write_no_flush(buf, len) cdc_write_optimized(buf, len, false)

void serial_comm_manager_init(RP2040_STATE* rp2040_state){
    rp2040_state_ = *rp2040_state;
    incoming_packet_from_android.start_marker = START_MARKER;
    incoming_packet_from_android.end_marker = END_MARKER;
    outgoing_packet_to_android.start_marker = START_MARKER;
    outgoing_packet_to_android.end_marker = END_MARKER;
    outgoing_packet_to_android.data_size = sizeof(rp2040_state_);
    outgoing_log_packet_to_android.start_marker = START_MARKER;
    outgoing_log_packet_to_android.packet_type = GET_LOG;
    outgoing_log_packet_to_android.end_marker = END_MARKER;
    outgoing_version_packet_to_android.start_marker = START_MARKER;
    outgoing_version_packet_to_android.packet_type = GET_VERSION;
    outgoing_version_packet_to_android.data_size = sizeof(VERSION);
    outgoing_version_packet_to_android.end_marker = END_MARKER;
}

static void reset_packet_and_send_nack(int8_t *start_idx, int8_t *end_idx, uint16_t *buffer_index) {
    *start_idx = -1;
    *end_idx = -1;
    *buffer_index = 0;
    memset(&incoming_packet_from_android, 0, sizeof(IncomingPacketFromAndroid));
    outgoing_packet_to_android.packet_type = NACK;
    cdc_write(&outgoing_packet_to_android, sizeof(outgoing_packet_to_android));
}

// reads data from the UART and stores it in buffer. If no data is available, returns immediately.
// if new data is available, reads it until the buffer is full or both start and stop markers detected
// calls handle_block to process the data if both markers are detected
void get_block() {
    // initialize as -1 as a way of detecting the absence of each marker in the buffer
    static int8_t start_idx = -1;
    static int8_t end_idx = -1;
    uint16_t buffer_index= 0;
    uint8_t i = 0;
    uint8_t MAX_SERIAL_GET_COUNT = 100;

    int c = getchar_timeout_us(100);
    // Only process data after finding the START_MARKER
    if (c != PICO_ERROR_TIMEOUT && c == START_MARKER){
        start_idx = buffer_index;
        // After finding the start marker get the rest of the packet or until MAX_SERIAL_GET_COUNT
        // to prevent an infinite loop
        // + 1 to accomodate for the packet_type
        while (buffer_index < (ANDROID_BUFFER_LENGTH_IN + 1) || i == MAX_SERIAL_GET_COUNT) {
            c = getchar_timeout_us(100);

            if (c != PICO_ERROR_TIMEOUT){
                if (buffer_index == 0){
                    // First byte after start marker is the command
                    incoming_packet_from_android.packet_type = (c & 0xFF);
                    buffer_index++;
                }else{
                    // -2 to accomodate for the start marker and packet_type
                    incoming_packet_from_android.data[buffer_index - 1] = (c & 0xFF);
                    buffer_index++;
                }
            }else {
                rp2040_log("Timeout while reading packet. Resetting state.\n");
                reset_packet_and_send_nack(&start_idx, &end_idx, &buffer_index);
                return;
            }
            i++;
        }
        c = getchar_timeout_us(100);
        if (c != PICO_ERROR_TIMEOUT && c == END_MARKER){
            // Calculate the length of the packet
            uint16_t packet_length = end_idx - start_idx;
            if (packet_length >= sizeof(IncomingPacketFromAndroid)) {
                // Call the handle_block function with the packet data
                handle_packet(&incoming_packet_from_android);
                // Reset the values of start and end idx to detect the next block
                start_idx = -1;
                end_idx = -1;
                buffer_index = 0;
                // Reset the packet
                memset(&incoming_packet_from_android, 0, sizeof(IncomingPacketFromAndroid)); } else {
                rp2040_log("Received incomplete packet. Resetting state.\n");
                reset_packet_and_send_nack(&start_idx, &end_idx, &buffer_index);
                return;
            }
        }else{
            rp2040_log("Received packet with no end marker. Resetting state.\n");
            reset_packet_and_send_nack(&start_idx, &end_idx, &buffer_index);
            return;
        }

    }
}

/**
 * Wrapper for log flushing that respects the optimized CDC write's timeout
 */
static bool log_flush_optimized_wrapper(const void* buf, uint32_t len) {
    return cdc_write_no_flush(buf, len);
}

void handle_packet(IncomingPacketFromAndroid *packet){
    // Assign the same packet type to the outgoing packet for verification on Android end
    switch (packet->packet_type){
        case GET_LOG:
            outgoing_log_packet_to_android.packet_type = packet->packet_type;

            rp2040_log_acquire_lock();
            outgoing_log_packet_to_android.data_size = rp2040_get_byte_count();

            // COALESCE: Build header in a single buffer to reduce USB frames
            uint8_t header[4];
            header[0] = outgoing_log_packet_to_android.start_marker;
            header[1] = outgoing_log_packet_to_android.packet_type;
            header[2] = (uint8_t)(outgoing_log_packet_to_android.data_size & 0xFF);
            header[3] = (uint8_t)((outgoing_log_packet_to_android.data_size >> 8) & 0xFF);

            if (!cdc_write_no_flush(header, 4)) {
                rp2040_log_release_lock();
                break;
            }

            // Use the wrapper to flush logs via the optimized path
            rp2040_log_flush(log_flush_optimized_wrapper);
            rp2040_log_release_lock();

            cdc_write(&outgoing_log_packet_to_android.end_marker, 1);
            break;

        case SET_MOTOR_LEVEL:
            outgoing_packet_to_android.packet_type = packet->packet_type;
            // Clear the state
            memset(&rp2040_state_, 0, sizeof(rp2040_state_));
            // Copy the motor levels from the packet to the rp2040_state_
            memcpy(&rp2040_state_.MotorsState.ControlValues.left, &packet->data[0], sizeof(uint8_t));
            memcpy(&rp2040_state_.MotorsState.ControlValues.right, &packet->data[1], sizeof(uint8_t));
            set_motor_levels(&rp2040_state_);
            // Add STATE to response
            get_state(&rp2040_state_);
            outgoing_packet_to_android.data = rp2040_state_;

            // Use direct CDC write for the whole packet
            cdc_write(&outgoing_packet_to_android, sizeof(outgoing_packet_to_android));
            break;
        case GET_STATE:
            outgoing_packet_to_android.packet_type = packet->packet_type;
            // Clear the state
            memset(&rp2040_state_, 0, sizeof(rp2040_state_));
            // Add STATE to response
            get_state(&rp2040_state_);
            outgoing_packet_to_android.data = rp2040_state_;

            // Use direct CDC write for the whole packet
            cdc_write(&outgoing_packet_to_android, sizeof(outgoing_packet_to_android));
            break;
        case GET_VERSION:
            outgoing_version_packet_to_android.packet_type = packet->packet_type;
            outgoing_version_packet_to_android.data.version_major = FW_VERSION_MAJOR;
            outgoing_version_packet_to_android.data.version_minor = FW_VERSION_MINOR;
            outgoing_version_packet_to_android.data.version_patch = FW_VERSION_PATCH;

            cdc_write_optimized(&outgoing_version_packet_to_android, sizeof(outgoing_version_packet_to_android), true);
            break;
        case RESET_STATE:
            // TODO
            break;
        default:
            outgoing_packet_to_android.packet_type = NACK;
            cdc_write(&outgoing_packet_to_android, sizeof(outgoing_packet_to_android));
            break;
    }
}
