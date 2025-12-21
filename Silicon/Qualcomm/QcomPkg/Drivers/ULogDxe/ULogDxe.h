#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include "vaarg-altabi.h"

typedef struct ULogHandle_s* ULogHandle;
typedef int ULogResult;

typedef EFI_STATUS (EFIAPI*ULOG_DUMMY)();

typedef EFI_STATUS (EFIAPI* ULOG_RAWINIT)(
  ULogResult *ret,
  ULogHandle * h,
  const char * name,
  UINT32 logBufSize,
  UINT32 logBufMemType,
  int logLockType
);

typedef EFI_STATUS (EFIAPI* ULOG_RAWLOG)(
  ULogResult *ret,
  ULogHandle h,
  const char * data,
  UINT32 length
);

typedef EFI_STATUS (EFIAPI* ULOG_REALTIMEINIT)(
  ULogResult *ret,
  ULogHandle * h,
  const char * name,
  UINT32 logBufSize,
  UINT32 logBufMemType,
  int logLockType
);

typedef EFI_STATUS (EFIAPI* ULOG_REALTIMEVPRINTF)(
  ULogResult *ret,
  ULogHandle h,
  UINT32 arg_count,
  const char * fmt,
  A2_VA_LIST ap
);

typedef EFI_STATUS (EFIAPI* ULOG_REALTIMEVPRINTF_EX)(
  ULogResult *ret,
  ULogHandle h,
  UINT32 arg_count,
  UINT32 isarg64bitmask,
  const char * fmt,
  A2_VA_LIST ap
);

typedef struct _EFI_ULOG_PROTOCOL {
  UINT64 Revision;
//ULog.c
  ULOG_DUMMY f1;
  ULOG_DUMMY f2;
  ULOG_DUMMY f3;
  ULOG_DUMMY f4;
  ULOG_DUMMY f5;
  ULOG_DUMMY f6;
  ULOG_DUMMY f7;
  ULOG_DUMMY f8;
  ULOG_DUMMY f9;
  ULOG_DUMMY f10;
  ULOG_DUMMY f11;
  ULOG_DUMMY f12;
  ULOG_DUMMY f13;
  ULOG_DUMMY f14;
  ULOG_DUMMY f15;
  ULOG_DUMMY f16;
  ULOG_DUMMY f17;
  ULOG_DUMMY f18;
  ULOG_DUMMY f19;
  ULOG_DUMMY f20;
  ULOG_DUMMY f21;
  ULOG_DUMMY f22;
  ULOG_DUMMY f23;
  ULOG_DUMMY f24;
  ULOG_DUMMY f25;
  ULOG_DUMMY f26;
  ULOG_DUMMY f27;

  ULOG_RAWINIT RawInit;
  ULOG_RAWLOG RawLog;
  ULOG_REALTIMEINIT RealTimeInit;
  ULOG_REALTIMEVPRINTF RealTimeVprintf;
  ULOG_DUMMY f28;
  ULOG_DUMMY f29;
  ULOG_DUMMY f30;
  ULOG_DUMMY f31;
  ULOG_DUMMY f32;
  ULOG_DUMMY f33;
  ULOG_DUMMY f34;
  ULOG_REALTIMEVPRINTF_EX RealTimeVprintf_Ex;

  ULOG_DUMMY f35;
  ULOG_DUMMY f36;
  ULOG_DUMMY f37;

  ULOG_DUMMY f38;
} EFI_ULOG_PROTOCOL;
