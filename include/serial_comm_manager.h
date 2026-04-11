#ifndef SERIAL_COMM_MANAGER_H
#define SERIAL_COMM_MANAGER_H

#include "hardware/platform_defs.h"
#include "pico/types.h"
#include "rp2040_log.h"

// TinyFrame Type IDs (Commands)
#define GET_LOG 0x00
#define SET_MOTOR_LEVEL 0x04
#define RESET_STATE 0x02
#define GET_STATE 0x03
#define GET_VERSION 0x06

// Status codes
#define NACK 0xFC
#define ACK 0xFD

#pragma pack(1) // Set packing alignment to 1 byte
typedef struct
{
    struct
    {
        struct
        {
            uint8_t left;
            uint8_t right;
        } ControlValues;

        struct
        {
            // See drv8830 datasheet Table 8.
            uint8_t left;
            uint8_t right;
        } Faults;

        struct
        {
            uint32_t left;
            uint32_t right;
        } EncoderCounts;
    } MotorsState;

    struct
    {
	uint16_t voltage;
	uint8_t safety_status;
	uint16_t temperature;
	uint8_t state_of_health;
	uint16_t flags;
    } BatteryDetails;

    struct
    {
	uint32_t max77976_chg_details;
        uint8_t wireless_charger_attached;
	uint16_t usb_charger_voltage;
	uint16_t wireless_charger_vrect;
    } ChargeSideUSB;


} RP2040_STATE;

typedef struct
{
    uint8_t version_major;
    uint8_t version_minor;
    uint8_t version_patch;
} VERSION;
#pragma pack() // Reset packing alignment to default

void get_block();
void serial_comm_manager_init(RP2040_STATE* state);

#endif
