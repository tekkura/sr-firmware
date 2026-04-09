#include "pico/types.h"
#include "serial_comm_manager.h"
#include "pico/stdio.h"
#include <string.h>
#include <stdio.h>
#include "robot.h"
#include "rp2040_log.h"
#include "TinyFrame.h"
#include "version.h"

static TinyFrame tf;
static RP2040_STATE rp2040_state_;

/**
 * TinyFrame Write Implementation
 * Sends data to the Android host via stdio (USB/UART)
 */
void TF_WriteImpl(TinyFrame *tf, const uint8_t *buff, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        putchar(buff[i]);
    }
}

// Helper for streaming logs through TinyFrame multipart
static void tf_log_stream_cb(void* ctx, const uint8_t* buff, uint32_t len) {
    TF_Multipart_Payload((TinyFrame*)ctx, buff, len);
}

// Helper for sending a NACK response
static void send_nack(TinyFrame *tf, TF_ID id) {
    TF_Msg response;
    TF_ClearMsg(&response);
    response.type = NACK;
    response.frame_id = id;
    response.is_response = true;
    TF_Respond(tf, &response);
}

// Forward declarations for TinyFrame listeners
TF_Result listener_get_log(TinyFrame *tf, TF_Msg *msg);
TF_Result listener_set_motor_level(TinyFrame *tf, TF_Msg *msg);
TF_Result listener_get_state(TinyFrame *tf, TF_Msg *msg);
TF_Result listener_reset_state(TinyFrame *tf, TF_Msg *msg);
TF_Result listener_get_version(TinyFrame *tf, TF_Msg *msg);
TF_Result generic_listener(TinyFrame *tf, TF_Msg *msg);

void serial_comm_manager_init(RP2040_STATE* rp2040_state){
    rp2040_state_ = *rp2040_state;

    // Initialize TinyFrame as a Slave (Pico side)
    TF_InitStatic(&tf, TF_SLAVE);

    // Register Type Listeners for the protocol commands
    TF_AddTypeListener(&tf, GET_LOG, listener_get_log);
    TF_AddTypeListener(&tf, SET_MOTOR_LEVEL, listener_set_motor_level);
    TF_AddTypeListener(&tf, GET_STATE, listener_get_state);
    TF_AddTypeListener(&tf, RESET_STATE, listener_reset_state);
    TF_AddTypeListener(&tf, GET_VERSION, listener_get_version);

    // Register a Generic Listener to catch unknown commands and send NACK
    TF_AddGenericListener(&tf, generic_listener);
}

/**
 * Refactored get_block: Feeds characters to TinyFrame
 * This replaces the manual state machine with TinyFrame's parser.
 */
void get_block() {
    int c;
    
    // Provide a timebase for TinyFrame timeouts
    static uint32_t last_tick = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now != last_tick) {
        TF_Tick(&tf);
        last_tick = now;
    }

    // Read all available characters from the serial buffer and feed them to the TF state machine
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        TF_AcceptChar(&tf, (uint8_t)c);
    }
}

// --- TinyFrame Listeners Implementation ---

/**
 * Unhandled message listener: sends NACK
 */
TF_Result generic_listener(TinyFrame *tf, TF_Msg *msg) {
    send_nack(tf, msg->frame_id);
    return TF_STAY;
}

/**
 * Sends back the circular buffer logs as a multipart message
 */
TF_Result listener_get_log(TinyFrame *tf, TF_Msg *msg) {
    TF_Msg response;
    TF_ClearMsg(&response);
    response.type = msg->type;
    response.frame_id = msg->frame_id;
    response.is_response = true;

    rp2040_log_acquire_lock();
    uint16_t log_size = rp2040_get_byte_count();
    response.len = log_size;
    response.data = NULL; // will use multipart

    TF_Respond_Multipart(tf, &response);
    rp2040_log_flush_cb(tf_log_stream_cb, tf);
    TF_Multipart_Close(tf);
    rp2040_log_release_lock();

    return TF_STAY;
}

/**
 * Sets motor levels and responds with the updated robot state
 */
TF_Result listener_set_motor_level(TinyFrame *tf, TF_Msg *msg) {
    if (msg->len == 2) {
        // Clear the local state cache
        memset(&rp2040_state_, 0, sizeof(rp2040_state_));

        // Copy the motor levels from the received payload
        rp2040_state_.MotorsState.ControlValues.left = msg->data[0];
        rp2040_state_.MotorsState.ControlValues.right = msg->data[1];

        // Apply to hardware
        set_motor_levels(&rp2040_state_);

        // Refresh full state for response
        get_state(&rp2040_state_);

        TF_Msg response;
        TF_ClearMsg(&response);
        response.type = msg->type;
        response.frame_id = msg->frame_id;
        response.is_response = true;
        response.data = (uint8_t*)&rp2040_state_;
        response.len = sizeof(rp2040_state_);

        TF_Respond(tf, &response);
    } else {
        // Explicitly send NACK if the command data is invalid (wrong fixed payload size)
        send_nack(tf, msg->frame_id);
    }
    return TF_STAY;
}

/**
 * Responds with the current full robot state
 */
TF_Result listener_get_state(TinyFrame *tf, TF_Msg *msg) {
    // Refresh full state
    memset(&rp2040_state_, 0, sizeof(rp2040_state_));
    get_state(&rp2040_state_);

    TF_Msg response;
    TF_ClearMsg(&response);
    response.type = msg->type;
    response.frame_id = msg->frame_id;
    response.is_response = true;
    response.data = (uint8_t*)&rp2040_state_;
    response.len = sizeof(rp2040_state_);

    TF_Respond(tf, &response);
    return TF_STAY;
}

/**
 * Reset robot state placeholder implementation
 */
TF_Result listener_reset_state(TinyFrame *tf, TF_Msg *msg) {
    // Perform hardware reset logic here
    // For now, we respond with ACK or current state

    TF_Msg response;
    TF_ClearMsg(&response);
    response.type = msg->type;
    response.frame_id = msg->frame_id;
    response.is_response = true;

    // We could send back an empty ACK packet or current state
    get_state(&rp2040_state_);
    response.data = (uint8_t*)&rp2040_state_;
    response.len = sizeof(rp2040_state_);

    TF_Respond(tf, &response);
    return TF_STAY;
}

/**
 * Responds with the firmware version
 */
TF_Result listener_get_version(TinyFrame *tf, TF_Msg *msg) {
    TF_Msg response;
    TF_ClearMsg(&response);
    response.type = msg->type;
    response.frame_id = msg->frame_id;
    response.is_response = true;

    VERSION version;
    version.version_major = FW_VERSION_MAJOR;
    version.version_minor = FW_VERSION_MINOR;
    version.version_patch = FW_VERSION_PATCH;

    response.data = (uint8_t*)&version;
    response.len = sizeof(version);

    TF_Respond(tf, &response);
    return TF_STAY;
}
