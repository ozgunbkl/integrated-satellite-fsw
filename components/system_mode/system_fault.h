#ifndef SYSTEM_FAULT_H
#define SYSTEM_FAULT_H

#include <stdbool.h>
#include <stdint.h>

#define SYSTEM_FAULT_CAPACITY 16U

// Generic keys: the reporting service validates its source/code namespace.
typedef struct {
    uint8_t source;
    bool fatal;
    uint16_t code;
    uint32_t first_timestamp;
    uint32_t last_timestamp;
    uint32_t reports;
    uint32_t revision;
} SystemFaultRecord_t;

typedef struct {
    uint16_t count;
    bool overflow;
    bool recording_uncertain;
    SystemFaultRecord_t records[SYSTEM_FAULT_CAPACITY];
} SystemFaultSnapshot_t;

#endif
