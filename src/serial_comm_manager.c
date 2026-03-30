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
 */
static void cdc_write_optimized(const void* buf, uint32_t len) {
    // Check if CDC is connected to avoid blocking or errors
    if (!tud_cdc_n_connected(0)) return;

    uint32_t written = tud_cdc_n_write(0, buf, len);
    (void)written; // Suppression of unused variable warning

    // Force immediate transmission of the USB frame
    tud_cdc_n_write_flush(0);
}

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
    cdc_write_optimized(&outgoing_packet_to_android, sizeof(outgoing_packet_to_android));
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

void handle_packet(IncomingPacketFromAndroid *packet){
    // Assign the same packet type to the outgoing packet for verification on Android end
    switch (packet->packet_type){
        case GET_LOG:
            outgoing_log_packet_to_android.packet_type = packet->packet_type;

            cdc_write_optimized(&outgoing_log_packet_to_android.start_marker, 1);
            cdc_write_optimized(&outgoing_log_packet_to_android.packet_type, 1);

            rp2040_log_acquire_lock();
            outgoing_log_packet_to_android.data_size = rp2040_get_byte_count();

            cdc_write_optimized(&outgoing_log_packet_to_android.data_size, 2);

            // Use the callback to flush logs directly via the optimized path
            rp2040_log_flush(cdc_write_optimized);
            rp2040_log_release_lock();

            cdc_write_optimized(&outgoing_log_packet_to_android.end_marker, 1);
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
            cdc_write_optimized(&outgoing_packet_to_android, sizeof(outgoing_packet_to_android));
            break;
        case GET_STATE:
            outgoing_packet_to_android.packet_type = packet->packet_type;
            // Clear the state
            memset(&rp2040_state_, 0, sizeof(rp2040_state_));
            // Add STATE to response
            get_state(&rp2040_state_);
            outgoing_packet_to_android.data = rp2040_state_;

            // Use direct CDC write for the whole packet
            cdc_write_optimized(&outgoing_packet_to_android, sizeof(outgoing_packet_to_android));
            break;
        case GET_VERSION:
            outgoing_version_packet_to_android.packet_type = packet->packet_type;
            outgoing_version_packet_to_android.data.version_major = FW_VERSION_MAJOR;
            outgoing_version_packet_to_android.data.version_minor = FW_VERSION_MINOR;
            outgoing_version_packet_to_android.data.version_patch = FW_VERSION_PATCH;

            cdc_write_optimized(&outgoing_version_packet_to_android, sizeof(outgoing_version_packet_to_android));
            break;
        case RESET_STATE:
            // TODO
            break;
        default:
            outgoing_packet_to_android.packet_type = NACK;
            cdc_write_optimized(&outgoing_packet_to_android, sizeof(outgoing_packet_to_android));
            break;
    }
}
