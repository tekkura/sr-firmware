# Communication Protocol Contract (v1.0.0)

This document defines the communication contract between the RP2040 firmware and the Android Host application using the custom Length-Prefix + CRC framing.

## 1. Transport Layer (Length-Prefix + CRC16)
The protocol uses a lightweight custom framing structure designed for high-speed serial reliability.

### Frame Format
| Byte Offset    | Field          | Size    | Description                                                    |
|:---------------|:---------------|:--------|:---------------------------------------------------------------|
| 0              | `START_MARKER` | 1 byte  | Constant `0xFE` (SOH - Start of Header)                        |
| 1-2            | `LENGTH`       | 2 bytes | Total length of Payload + Command ID (**Little-Endian**)       |
| 3              | `COMMAND_ID`   | 1 byte  | Command Type (e.g., `0x01` for SET_MOTOR)                      |
| 4 to (N+3)     | `PAYLOAD`      | N bytes | Actual data bytes                                              |
| (N+4) to (N+5) | `CRC16`        | 2 bytes | CRC16-CCITT checksum over bytes 1 to (N+3) (**Little-Endian**) |

### Configuration
*   **CRC Polynomial**: `0x1021` (x^16 + x^12 + x^5 + 1)
*   **Initial CRC Value**: `0xFFFF`
*   **Endianness**: All multi-byte fields (`LENGTH`, `CRC16`, and internal struct fields) use **Little-Endian** byte order.
*   **Log Line Count**: Set to 500 to avoid length and crc16 overflow

## 2. Command Types (Message IDs)
| Hex ID | Name              | Description                                                        | Response Payload |
|:-------|:------------------|:-------------------------------------------------------------------|:-----------------|
| `0x00` | `GET_LOG`         | Requests circular buffer logs                                      | `Raw String`     |
| `0x01` | `SET_MOTOR_LEVEL` | Sets [Left, Right] PWM                                             | `RP2040_STATE`   |
| `0x03` | `GET_STATE`       | Requests current sensor/motor state                                | `RP2040_STATE`   |
| `0x02` | `RESET_STATE`     | Currently does nothing                                             | `ACK (0xFD)`     |
| `0xFD` | `ACK`             | Only supposed to be used as a response                             | None             |
| `0xFC` | `NACK`            | Integrity error (CRC mismatch, invalid command ID or payload size) | None             |

## 3. Data Structures
All structures are **Packed (1-byte alignment)** and use **Little-Endian** byte order for internal fields.

### `RP2040_STATE` (Current Size: 29 bytes)
| Offset | Field                 | Type       | Description                 |
|:-------|:----------------------|:-----------|:----------------------------|
| 0      | `ControlValues.left`  | `uint8_t`  | Left motor PWM (0-255)      |
| 1      | `ControlValues.right` | `uint8_t`  | Right motor PWM (0-255)     |
| 2      | `Faults.left`         | `uint8_t`  | DRV8830 Fault bits          |
| 3      | `Faults.right`        | `uint8_t`  | DRV8830 Fault bits          |
| 4      | `EncoderCounts.left`  | `uint32_t` | Cumulative encoder ticks    |
| 8      | `EncoderCounts.right` | `uint32_t` | Cumulative encoder ticks    |
| ...    | ...                   | ...        | See `serial_comm_manager.h` |

## 4. Android-Side Implementation Guide
*   **Framing Logic**: The Android library must follow the structure in Section 1, specifically calculating the CRC over the length field, command ID, and data payload.
*   **CRC16-CCITT Implementation**: Must use the same CRC16 polynomial (`0x1021`) and initial value (`0xFFFF`) as the firmware.
*   **Raw Serial Stream**: The USB-to-Serial driver must be configured with **no CR/LF translation** and 8N1 settings.
*   **Little-Endian Struct Parsing**: When decoding `RP2040_STATE`, you **must** use `ByteBuffer.order(ByteOrder.LITTLE_ENDIAN)`.
