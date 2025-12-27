#include <Protocol/ULogCtl.h>
#include <Library/PrintLib.h>
#include <Library/BaseLib.h>
#include "ULogDxe.h"
#include "mysnprintf.h"

static EFI_ULOG_PROTOCOL mULogProtocol; // Initialisation follows later
static EFI_ULOG_CTL_PROTOCOL mULogCtlProtocol; // "
static char buf[0xFF];

#define MAX_LOGS 64

struct logfile_t {
  const char* name;
  unsigned enabled : 1;
};

static unsigned long id = 1;
static struct logfile_t logfile[MAX_LOGS] = {
  { .enabled = 0 },
};


static int mystrlcpy(char* out, unsigned long len, const char* in){
  if(!len || !out)
    return 0;
  len -= 1;
  unsigned long i=0;
  if(in)
    for(; i<len && *in; i++)
      out[i] = in[i];
  out[i] = 0;
  return i;
}


EFI_STATUS EFIAPI DefaultStub1 (ULogResult *ret, ...) {
  DEBUG ((EFI_D_WARN, "DefaultStub1\n"));
  if(ret)
    *ret = 0;
  return 0;
}

EFI_STATUS EFIAPI DefaultStub2 () {
  DEBUG ((EFI_D_WARN, "DefaultStub2\n"));
  return 0;
}

EFI_STATUS EFIAPI ULogDxeInit (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status = 0;

  for(ULOG_DUMMY* it = &mULogProtocol.f1; ((char*)it)+(sizeof(void(*)())-1) < (char*)(&mULogProtocol+1); it++){
    if(!*it)
      *it = (ULOG_DUMMY)DefaultStub1;
  }

  Status = gBS->InstallMultipleProtocolInterfaces( &ImageHandle,
                  &gEfiULogProtocolGuid, &mULogProtocol,
                  &gULogCtlProtocolGuid, &mULogCtlProtocol,
                  NULL);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Install ULog Protocol! Status = %r\n", Status));
    Status = -1;
    goto end;
  }

end:
  return Status;
}

static unsigned long register_logfile(const char* name){
  if(!name)
    return 0;
  for(int i=1, n=id<MAX_LOGS?id:MAX_LOGS; i<n; i++)
    if(AsciiStrnCmp(name, logfile[i].name, 24) == 0)
      return i;
  const unsigned long i = id++;
  char buf[25] = {0};
  mystrlcpy(buf, sizeof(buf), name);
  DEBUG ((EFI_D_WARN, "Log %lu initialized: \"%a\"\n", i, buf)); // Note: no, %.24a does not work.
  if(i < MAX_LOGS)
    logfile[i].name = name;
  return i;
}

static EFIAPI EFI_STATUS EnableLog(const char* name, int enabled){
  unsigned long i = register_logfile(name);
  if(i >= MAX_LOGS){
    DEBUG ((EFI_D_WARN, "Too many logfiles have been used\n"));
    return EFI_OUT_OF_RESOURCES;
  }
  logfile[i].enabled = enabled;
  return EFI_SUCCESS;
}

static EFIAPI EFI_STATUS LogInit(
  ULogResult * ret,
  ULogHandle * h,
  const char * name,
  UINT32 logBufSize,
  UINT32 logBufMemType,
  int logLockType
){
  if(!h){
    if(ret) *ret = -24;
    return EFI_INVALID_PARAMETER;
  }
  if(!name)
    name = "(null)";
  const unsigned long id = register_logfile(name);
  *h = (ULogHandle)(id*sizeof(ULogHandle));
  if(ret) *ret = 0;
  return EFI_SUCCESS;
}

static EFIAPI EFI_STATUS RealTimeVprintf_Ex(
  ULogResult *ret,
  ULogHandle h,
  UINT32 arg_count,
  UINT32 isarg64bitmask,
  const char * fmt,
  A2_VA_LIST ap
){
  if(!h || !fmt){
    if(ret) *ret = -24;
    return EFI_INVALID_PARAMETER;
  }
  const int id = ((UINTN)h)/sizeof(ULogHandle);
  const BOOLEAN enabled = logfile[0].enabled || (id < MAX_LOGS && logfile[id].enabled);
  if(enabled){
    // DEBUG((EFI_D_WARN, "%08X %u %p %a\n", isarg64bitmask, arg_count, fmt, fmt));
    myulogprintf(buf, sizeof(buf), arg_count, isarg64bitmask, fmt, ap);
    DEBUG((EFI_D_WARN, "%a\n", buf));
  }
  if(ret) *ret = 0;
  return EFI_SUCCESS;
}

static EFIAPI EFI_STATUS RealTimeVprintf(
  ULogResult *ret,
  ULogHandle h,
  UINT32 arg_count,
  const char * fmt,
  A2_VA_LIST ap
){
  return RealTimeVprintf_Ex(ret, h, arg_count, 0, fmt, ap);
}

static EFIAPI EFI_STATUS RawLog(
  ULogResult *ret,
  ULogHandle h,
  const char * data,
  UINT32 length
){
  mystrlcpy(buf, length < sizeof(buf) ? length : sizeof(buf), data);
  DEBUG((EFI_D_WARN, "%a\n", buf));
  return EFI_SUCCESS;
}

static EFI_ULOG_PROTOCOL mULogProtocol = {
  .Revision = 0x10005,
  .RawInit = LogInit,
  .RealTimeInit = LogInit,
  .RawLog = RawLog,
  .RealTimeVprintf = RealTimeVprintf,
  .RealTimeVprintf_Ex = RealTimeVprintf_Ex,
  .f18 = DefaultStub2,
};

static EFI_ULOG_CTL_PROTOCOL mULogCtlProtocol = {
  .EnableLog = EnableLog,
};

