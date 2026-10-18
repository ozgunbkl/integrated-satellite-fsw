// lib/hal_mock/hal.c
#include "hal.h"
#include "hal_config.h"



// The "Hardware": Mock Register Array


static uint32_t s_mock_registers[TOTAL_MOCK_REGS] = {0};

// Every uint8_t address is valid. Changing the bank size requires revisiting
// this API, rather than retaining an always-true runtime bounds check.
_Static_assert(TOTAL_MOCK_REGS == UINT8_MAX + 1U,
               "The mock register bank must cover every uint8_t address");

uint32_t HAL_REG_Read(uint8_t address) {
    return s_mock_registers[address];
}

void HAL_REG_Write(uint8_t address, uint32_t value) {
    s_mock_registers[address] = value;
}


// NOTE: In a real system, init sets the direction (Input/Output).
// For this simple mock, i just use the pin number as the bit index.
void HAL_GPIO_Init(uint8_t pin, bool is_output) {
    // In the mock, i can just ensure the pin number is valid
    // In a real HAL, this would configure the direction register (DIR).
    if (pin >= 32) return; // Only supporting pins 0-31 for simplicity
    
    // If output, set initial state to low in the output register
    if (is_output) {
        HAL_GPIO_Write(pin, false);
    }
}

// Function to simulate setting a GPIO pin's output state
void HAL_GPIO_Write(uint8_t pin, bool state) {
    if (pin >= 32) return;

    // 1. Read the current output register value
    uint32_t current_state = HAL_REG_Read(MOCK_REG_GPIO_OUTPUT);
    
    if (state == true) {
        // 2. Set the 'pin' bit high (simulate turning it ON)
        current_state |= (1UL << pin);
    } else {
        // 3. Set the 'pin' bit low (simulate turning it OFF)
        current_state &= ~(1UL << pin);
    }
    
    // 4. Write the new value back to the mock register
    HAL_REG_Write(MOCK_REG_GPIO_OUTPUT, current_state);
}
// 

// Function to simulate reading a GPIO pin's input state
bool HAL_GPIO_Read(uint8_t pin) {
    if (pin >= 32) return false;

    // For a mock: Let's assume input reads from MOCK_REG_GPIO_INPUT
    // and output reads from MOCK_REG_GPIO_OUTPUT (loopback for testing)
    uint32_t input_register = HAL_REG_Read(MOCK_REG_GPIO_INPUT);
    
    // Simulate a loopback for testing by checking the output register if not set explicitly
    if (input_register == 0) {
        input_register = HAL_REG_Read(MOCK_REG_GPIO_OUTPUT);
    }

    // Check if the 'pin' bit is set (i.e., (1UL << pin) is NOT zero after the AND)
    return (input_register & (1UL << pin)) != 0;
}

// Simulates sending data (e.g., a command or register address) to a slave device
bool HAL_I2C_Master_Transmit(uint8_t slave_addr, const uint8_t* data, size_t len) {
    if (len == 0 || data == NULL) return false;

    // 1. Simulate setting the slave address/command register with the first byte of data
    // This mocks sending the command to the sensor.
    HAL_REG_Write(MOCK_REG_I2C_CMD, (slave_addr << 8) | data[0]);

    // 2. Simulate writing the data buffer (writing only the first byte to the mock register for simplicity)
    HAL_REG_Write(MOCK_REG_I2C_DATA, data[0]); 

    // 3. Simulate checking the status (always succeed unless the status register is explicitly set for NACK)
    uint32_t status = HAL_REG_Read(MOCK_REG_I2C_STATUS);
    
    // Check for NACK or return success
    if (status & I2C_STATUS_NACK) {
        return false;
    }
    return true;
}

// Simulates receiving a data buffer from a slave device
bool HAL_I2C_Master_Receive(uint8_t slave_addr, uint8_t *data, size_t len) {
    if (len == 0 || data == NULL) return false;

    // 1. Simulate setting the slave address for a read operation
    HAL_REG_Write(MOCK_REG_I2C_CMD, (slave_addr << 8) | 0x01); // 0x01 can mock a 'read' flag

    // 2. Simulate reading a known value from the mock data register
    uint32_t received_data = HAL_REG_Read(MOCK_REG_I2C_DATA);

    // 3. Place the received mock data into the user's buffer (the first byte only)
    data[0] = (uint8_t)received_data;

    // 4. Simulate checking the status
    uint32_t status = HAL_REG_Read(MOCK_REG_I2C_STATUS);
    if (status & I2C_STATUS_NACK) {
        return false;
    }
    return true; 
}


// Simulates transmitting a buffer of data via UART
bool HAL_UART_Transmit(const uint8_t* data, size_t len) {
    if (len == 0 || data == NULL) return false;

    // 1. Simulate checking if the transmitter is busy
    uint32_t status = HAL_REG_Read(MOCK_REG_UART_STATUS);
    if (status & UART_STATUS_TX_BUSY) {
        return false; // Transmitter is busy
    }

    // 2. Simulate writing the first byte of data to the transmit register
    HAL_REG_Write(MOCK_REG_UART_TX_DATA, data[0]);
    return true;
}

// Simulates receiving a buffer of data via UART
bool HAL_UART_Receive(uint8_t* data, size_t len) {
    if (len == 0 || data == NULL) return false;

    // 1. Simulate checking if data is ready to be read
    uint32_t status = HAL_REG_Read(MOCK_REG_UART_STATUS);
    if (!(status & UART_STATUS_RX_READY)) {
        return false; // No data available
    }

    // 2. Simulate reading the received data byte
    uint32_t received_data = HAL_REG_Read(MOCK_REG_UART_RX_DATA);

    // 3. Place the received mock data into the user's buffer (first byte only)
    data[0] = (uint8_t)received_data;

    // 4. Simulate clearing the ready flag after reading
    HAL_REG_Write(MOCK_REG_UART_STATUS, status & ~UART_STATUS_RX_READY);

    return true;
}

// Simulates a single SPI byte transfer: sends one byte and receives one byte simultaneously.
uint8_t HAL_SPI_Transfer(uint8_t slave_id, uint8_t transmit_byte) {
    
    // 1. Simulate selecting the correct slave device (CS pin)
    // Write the slave ID into the control register, combined with the select flag.
    HAL_REG_Write(MOCK_REG_SPI_CONTROL, SPI_CONTROL_SLAVE_SELECT | slave_id);
    
    // 2. Simulate writing the byte to be transmitted
    HAL_REG_Write(MOCK_REG_SPI_TX_DATA, transmit_byte);

    // 3. Simulate initiating the transfer (this would happen automatically on a real chip)
    HAL_REG_Write(MOCK_REG_SPI_CONTROL, HAL_REG_Read(MOCK_REG_SPI_CONTROL) | SPI_CONTROL_TRANSFER_START);

    // 4. In a real system, we would wait for the SPI_CONTROL_BUSY flag to clear.
    // For the mock, i assume the hardware returned a byte simultaneously.
    
    // 5. Simulate reading the received byte (which i assume the test setup populated)
    uint8_t received_byte = (uint8_t)HAL_REG_Read(MOCK_REG_SPI_RX_DATA);
    
    // 6. Simulate deselecting the slave device
    HAL_REG_Write(MOCK_REG_SPI_CONTROL, 0x00);

    return received_byte;
}
