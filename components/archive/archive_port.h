#ifndef ARCHIVE_PORT_H
#define ARCHIVE_PORT_H
#include <stdbool.h>
#include <stdint.h>

// Platform startup adapter. Nonzero per-boot discriminator, not an authentication key.
// Call only before application tasks. No NVS writes or persistent counter.
bool Archive_PortCreateBootId(uint64_t *boot_id);
#endif
