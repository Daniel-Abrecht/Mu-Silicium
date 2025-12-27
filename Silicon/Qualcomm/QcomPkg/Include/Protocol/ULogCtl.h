#ifndef ULOG_CTL_PROTOCOL_H
#define ULOG_CTL_PROTOCOL_H

// This is a different protocol from the ULog protocol, it's sole purpose is to allow enabling / disabling logs
// received over the ulog protocol and such things.

typedef EFIAPI EFI_STATUS EFI_ULOG_CTL_EnableLog(const char* name, int enabled);

typedef struct _EFI_ULOG_CTL_PROTOCOL {
  EFI_ULOG_CTL_EnableLog* EnableLog;
} EFI_ULOG_CTL_PROTOCOL;

#define ULOGCTL_ANY_LOG 0

#endif
