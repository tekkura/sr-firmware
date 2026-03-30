#ifndef CRC_H_
#define CRC_H_

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Calculates the CRC16-CCITT checksum for a buffer.
 * 
 * Polynomial: 0x1021 (x^16 + x^12 + x^5 + 1)
 * 
 * @param data Pointer to the data buffer
 * @param length Length of the data in bytes
 * @param initial Initial value (usually 0xFFFF for first call, then previous result)
 * @return uint16_t The calculated CRC16 checksum
 */
uint16_t crc16_ccitt(const uint8_t *data, size_t length, uint16_t initial);

#endif // CRC_H_
