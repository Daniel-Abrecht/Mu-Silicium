#ifndef _DEADLINE_LIB_H_
#define _DEADLINE_LIB_H_

#include <Uefi/UefiBaseType.h>

typedef struct {
  UINT64 end_time;
} Deadline;

EFI_STATUS Deadline_set(Deadline* t, UINT32 duration_ms);
BOOLEAN Deadline_has_expired(Deadline* t);

#endif
