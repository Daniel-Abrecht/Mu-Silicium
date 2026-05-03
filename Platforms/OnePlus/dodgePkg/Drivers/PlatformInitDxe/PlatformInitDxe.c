#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define VDP_GLINK_REMOTE_GUID_STR          "\xE2\xE4\x3F\x61""\xD6\x53""\x7f\x44""\x83\x7E\x65\xB9\x49\xE0\xD3\x00"
#define VDP_GLINK_CHANNEL_GUID_STR         "\xE2\xE4\x3F\x61""\xD6\x53""\x7f\x44""\x83\x7E\x65\xB9\x49\xE0\xD3\x01"
#define VDP_GLINK_UCSI_GUID_STR            "\xE2\xE4\x3F\x61""\xD6\x53""\x7f\x44""\x83\x7E\x65\xB9\x49\xE0\xD3\x02"
// The UCSI connector device protocol will be registred by the UCSI driver
#define VDP_UCSI_CONNECTOR_GUID_STR        "\xE2\xE4\x3F\x61""\xD6\x53""\x7f\x44""\x83\x7E\x65\xB9\x49\xE0\xD3\x03"
#define VDP_USB_REDRIVER_GUID_STR          "\xE2\xE4\x3F\x61""\xD6\x53""\x7f\x44""\x83\x7E\x65\xB9\x49\xE0\xD3\x04"
#define VDP_DWC3_CONTROLLER_GUID_STR       "\xE2\xE4\x3F\x61""\xD6\x53""\x7f\x44""\x83\x7E\x65\xB9\x49\xE0\xD3\x05"

#define HARDWARE_DEVICE_PATH_STR "\x01"
#define HW_VENDOR_DP_STR "\x04"


#define DP_END "\x7F\xFF\x04\x00"

#define DP_GLINK_REMOTE_LPASS \
  HARDWARE_DEVICE_PATH_STR HW_VENDOR_DP_STR "\x1F\x00" VDP_GLINK_REMOTE_GUID_STR "SMEM\0lpass\0" // xport remote
#define DP_GLINK_CHANNEL_ADSP_APPS \
  HARDWARE_DEVICE_PATH_STR HW_VENDOR_DP_STR "\x27\x00" VDP_GLINK_CHANNEL_GUID_STR "PMIC_RTR_ADSP_APPS\0" // channel

#define DP_GLINK_UCSI \
  HARDWARE_DEVICE_PATH_STR HW_VENDOR_DP_STR "\x16\x00" VDP_GLINK_UCSI_GUID_STR "\x0B\x80" // glink owner id
#define DP_GLINK_UCSI_CONNECTOR_1 \
  HARDWARE_DEVICE_PATH_STR HW_VENDOR_DP_STR "\x15\x00" VDP_UCSI_CONNECTOR_GUID_STR "\x01" // UCSI Connector 1

#define DP_USB_REDRIVER \
  HARDWARE_DEVICE_PATH_STR HW_VENDOR_DP_STR "\x14\x00" VDP_USB_REDRIVER_GUID_STR "" // TODO: Add base address information
#define DP_DWC3_USB_CONTROLLER \
  HARDWARE_DEVICE_PATH_STR HW_VENDOR_DP_STR "\x14\x00" VDP_DWC3_CONTROLLER_GUID_STR "" // TODO: Add base address information




EFI_STATUS EFIAPI Init(
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
){
  EFI_STATUS Status = 0;

  Status |= gBS->InstallMultipleProtocolInterfaces(&(EFI_HANDLE){0}, &gEfiDevicePathProtocolGuid,
    DP_GLINK_REMOTE_LPASS DP_END, NULL);
  Status |= gBS->InstallMultipleProtocolInterfaces(&(EFI_HANDLE){0}, &gEfiDevicePathProtocolGuid,
    DP_GLINK_REMOTE_LPASS DP_GLINK_CHANNEL_ADSP_APPS DP_END, NULL);
  Status |= gBS->InstallMultipleProtocolInterfaces(&(EFI_HANDLE){0}, &gEfiDevicePathProtocolGuid,
    DP_GLINK_REMOTE_LPASS DP_GLINK_CHANNEL_ADSP_APPS DP_GLINK_UCSI DP_END, NULL);

  Status |= gBS->InstallMultipleProtocolInterfaces(&(EFI_HANDLE){0}, &gEfiDevicePathProtocolGuid,
    DP_USB_REDRIVER DP_END, NULL);
  Status |= gBS->InstallMultipleProtocolInterfaces(&(EFI_HANDLE){0}, &gEfiDevicePathProtocolGuid,
    DP_DWC3_USB_CONTROLLER DP_END, NULL);

  return Status;
}
