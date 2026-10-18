// include/hal.h

#ifndef HAL_H
#define HAL_H

// RAM-backed test/demo register bank, NOT an ESP-IDF/STM32 hardware driver.
// I2C/UART mocks process only the first byte, even when len > 1.
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "hal_config.h"
// --- HAL_REG: Mock Register Access ---

// Full 8-bit address space (0..255); register definitions belong to hal_config.h.
uint32_t HAL_REG_Read(uint8_t address);
void HAL_REG_Write(uint8_t address, uint32_t value);

// --- HAL_GPIO: Digital I/O ---

void HAL_GPIO_Init(uint8_t pin, bool is_output);
void HAL_GPIO_Write(uint8_t pin, bool state);
bool HAL_GPIO_Read(uint8_t pin);

// --- HAL_I2C: Inter-Integrated Circuit ---

bool HAL_I2C_Master_Transmit(uint8_t slave_addr, const uint8_t* data, size_t length);
bool HAL_I2C_Master_Receive(uint8_t slave_addr, uint8_t *data, size_t len);

// --- HAL_UART: Universal Asynchronous Receiver/Transmitter ---
bool HAL_UART_Transmit(const uint8_t* data, size_t len);
bool HAL_UART_Receive(uint8_t* data, size_t len);

// --- HAL_SPI: Serial Peripheral Interface ---
uint8_t HAL_SPI_Transfer(uint8_t slave_id, uint8_t transmit_byte);

#endif // HAL_H
