#include "hal.h"
#include <assert.h>
#include <stdio.h>

static void reset_registers(void) {
    for (unsigned address = 0; address <= UINT8_MAX; ++address) {
        HAL_REG_Write((uint8_t)address, 0);
    }
}

static void check_registers(void) {
    for (unsigned address = 0; address <= UINT8_MAX; ++address) {
        assert(HAL_REG_Read((uint8_t)address) == 0);
        HAL_REG_Write((uint8_t)address, UINT32_C(0x5A000000) | address);
    }
    for (unsigned address = 0; address <= UINT8_MAX; ++address) {
        assert(HAL_REG_Read((uint8_t)address) == (UINT32_C(0x5A000000) | address));
    }
    HAL_REG_Write(UINT8_MAX, UINT32_MAX);
    assert(HAL_REG_Read(UINT8_MAX) == UINT32_MAX);
    assert(HAL_REG_Read(0) == UINT32_C(0x5A000000));
}

static void check_gpio(void) {
    reset_registers();
    HAL_GPIO_Write(0, true);
    HAL_GPIO_Write(31, true);
    assert(HAL_REG_Read(MOCK_REG_GPIO_OUTPUT) == UINT32_C(0x80000001));
    assert(HAL_GPIO_Read(0) && HAL_GPIO_Read(31));
    HAL_GPIO_Init(0, false);
    assert(HAL_GPIO_Read(0));
    HAL_GPIO_Init(31, true);
    assert(!HAL_GPIO_Read(31) && HAL_GPIO_Read(0));
    HAL_GPIO_Write(32, true);
    HAL_GPIO_Init(UINT8_MAX, true);
    assert(!HAL_GPIO_Read(32) && !HAL_GPIO_Read(UINT8_MAX));
    assert(HAL_REG_Read(MOCK_REG_GPIO_OUTPUT) == 1);
    HAL_REG_Write(MOCK_REG_GPIO_INPUT, UINT32_C(0x80000000));
    assert(!HAL_GPIO_Read(0) && HAL_GPIO_Read(31));
    HAL_GPIO_Write(0, false);
    assert(HAL_REG_Read(MOCK_REG_GPIO_OUTPUT) == 0);
}

static void check_i2c(void) {
    reset_registers();
    const uint8_t tx[] = {0xA5, 0x5A};
    uint8_t rx[] = {0x11, 0x22};
    assert(!HAL_I2C_Master_Transmit(0x42, NULL, 1));
    assert(!HAL_I2C_Master_Transmit(0x42, tx, 0));
    assert(!HAL_I2C_Master_Receive(0x42, NULL, 1));
    assert(!HAL_I2C_Master_Receive(0x42, rx, 0));
    assert(HAL_REG_Read(MOCK_REG_I2C_CMD) == 0);
    assert(HAL_REG_Read(MOCK_REG_I2C_DATA) == 0);
    assert(HAL_I2C_Master_Transmit(0x42, tx, sizeof(tx)));
    assert(HAL_REG_Read(MOCK_REG_I2C_CMD) == 0x42A5);
    assert(HAL_REG_Read(MOCK_REG_I2C_DATA) == tx[0]);
    assert(HAL_I2C_Master_Receive(0x42, rx, sizeof(rx)));
    assert(HAL_REG_Read(MOCK_REG_I2C_CMD) == 0x4201);
    assert(rx[0] == tx[0] && rx[1] == 0x22);
    HAL_REG_Write(MOCK_REG_I2C_STATUS, I2C_STATUS_NACK);
    assert(!HAL_I2C_Master_Transmit(0x42, tx, sizeof(tx)));
    assert(!HAL_I2C_Master_Receive(0x42, rx, sizeof(rx)));
}

static void check_uart(void) {
    reset_registers();
    const uint8_t tx[] = {0xA5, 0x5A};
    uint8_t rx[] = {0x11, 0x22};
    assert(!HAL_UART_Transmit(NULL, 1));
    assert(!HAL_UART_Transmit(tx, 0));
    assert(!HAL_UART_Receive(NULL, 1));
    assert(!HAL_UART_Receive(rx, 0));
    HAL_REG_Write(MOCK_REG_UART_STATUS, UART_STATUS_TX_BUSY);
    assert(!HAL_UART_Transmit(tx, sizeof(tx)));
    assert(HAL_REG_Read(MOCK_REG_UART_TX_DATA) == 0);
    assert(!HAL_UART_Receive(rx, sizeof(rx)));
    assert(rx[0] == 0x11 && rx[1] == 0x22);
    HAL_REG_Write(MOCK_REG_UART_STATUS, UART_STATUS_RX_READY | UART_STATUS_OVERRUN);
    HAL_REG_Write(MOCK_REG_UART_RX_DATA, 0x1C3);
    assert(HAL_UART_Transmit(tx, sizeof(tx)));
    assert(HAL_REG_Read(MOCK_REG_UART_TX_DATA) == tx[0]);
    assert(HAL_UART_Receive(rx, sizeof(rx)));
    assert(rx[0] == 0xC3 && rx[1] == 0x22);
    assert(HAL_REG_Read(MOCK_REG_UART_STATUS) == UART_STATUS_OVERRUN);
    assert(!HAL_UART_Receive(rx, sizeof(rx)));
}

static void check_spi(void) {
    reset_registers();
    HAL_REG_Write(MOCK_REG_SPI_RX_DATA, 0x1E7);
    assert(HAL_SPI_Transfer(3, 0xB6) == 0xE7);
    assert(HAL_REG_Read(MOCK_REG_SPI_TX_DATA) == 0xB6);
    assert(HAL_REG_Read(MOCK_REG_SPI_CONTROL) == 0);
}

int main(void) {
    check_registers();
    check_gpio();
    check_i2c();
    check_uart();
    check_spi();
    puts("PASS: all 256 mock registers and GPIO/I2C/UART/SPI contracts (not physical drivers).");
    return 0;
}
