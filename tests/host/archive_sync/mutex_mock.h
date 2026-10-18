#ifndef HOST_ARCHIVE_MUTEX_MOCK_H
#define HOST_ARCHIVE_MUTEX_MOCK_H
#include <stdbool.h>
void ArchiveMock_FailCreate(void);
void ArchiveMock_FailBootId(void);
void ArchiveMock_ZeroBootId(void);
void ArchiveMock_FailTake(void);
bool ArchiveMock_LockHeld(void);
void ArchiveMock_Hold(void);
void ArchiveMock_Release(void);
#endif
