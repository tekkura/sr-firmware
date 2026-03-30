# Communication Protocol Contract (v1.0.0)

This document defines the communication contract between the RP2040 firmware and the Android Host application.

## 1. Transport Layer (TinyFrame)
The protocol uses [TinyFrame](https://github.com/MightyPork/TinyFrame) (v2.3.0) for framing and data integrity.

### Configuration (`TF_Config.h`)
| Parameter         | Value            | Description                        |
|:------------------|:-----------------|:-----------------------------------|
| `TF_ID_BYTES`     | 1                | 1-byte incremental Frame ID        |
| `TF_LEN_BYTES`    | 2                | 2-byte Payload Length (Big-Endian) |
| `TF_TYPE_BYTES`   | 1                | 1-byte Command Type                |
| `TF_CKSUM_TYPE`   | `TF_CKSUM_CRC16` | CRC16-CCITT for header and payload |
| `TF_USE_SOF_BYTE` | 1                | Enabled                            |
| `TF_SOF_BYTE`     | `0x01`           | Start of Frame marker              |

## 2. Command Types (Message IDs)
| Hex ID | Name              | Description                         | Response Payload   |
|:-------|:------------------|:------------------------------------|:-------------------|
| `0x00` | `GET_LOG`         | Requests circular buffer logs       | `Multipart String` |
| `0x01` | `SET_MOTOR_LEVEL` | Sets [Left, Right] PWM              | `RP2040_STATE`     |
| `0x03` | `GET_STATE`       | Requests current sensor/motor state | `RP2040_STATE`     |
| `0x02` | `RESET_STATE`     | Resets firmware state machine       | `RP2040_STATE`     |
| `0xFC` | `NACK`            | Error indicator                     | None               |

## 3. Data Structures
All structures are **Packed (1-byte alignment)** and use **Little-Endian** byte order for internal fields (standard for ARM/Android).

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
*   **TinyFrame Configuration**: The Android library must be initialized with parameters matching Section 1 (e.g., `TF_LEN_BYTES=2`).
*   **CRC16-CCITT Implementation**: Must use the same CRC16 polynomial (0x1021) as the firmware.
*   **Raw Serial Stream**: The USB-to-Serial driver (e.g., `usb-serial-for-android`) must be configured with **no CR/LF translation** and 8N1 settings.
*   **Little-Endian Struct Parsing**: When decoding `RP2040_STATE`, you **must** use `ByteBuffer.order(ByteOrder.LITTLE_ENDIAN)`.