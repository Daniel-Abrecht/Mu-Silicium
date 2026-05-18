#ifndef DWC3_COMMON_H
#define DWC3_COMMON_H

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/EFIUCSIOPM.h>
#include <Protocol/NonDiscoverableDevice.h>

struct modeswitch_device;


struct device_binding {
  EFI_DRIVER_BINDING_PROTOCOL super;
  EFI_STATUS (*Init)(struct modeswitch_device* self, EFI_HANDLE handle);
};

extern struct device_binding USBModeSwitch_binding_protocol;
extern struct device_binding UCSIConnector_binding_protocol;
extern struct device_binding Redriver_binding_protocol;

extern struct device_binding*const binding_protocol_list[];

EFI_STATUS EFIAPI Binding_Start(
  IN EFI_DRIVER_BINDING_PROTOCOL *binding_protocol,
  IN EFI_HANDLE ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath OPTIONAL
);

struct ucsi_device {
  EFI_HANDLE* handle;
  EFI_UCSI_OPM_CONNECTOR_PROTOCOL* protocol;
};

struct redriver_device {
  EFI_HANDLE* handle;
};

enum usb_mode {
  USB_MODE_DISCONNECTED,
  USB_MODE_HOST,
  USB_MODE_DEVICE,
};
enum { USB_MODE_COUNT=3 };

struct modeswitch_device_usb_mode {
  const EFI_GUID* protocol_guid;
  void* protocol;
};

struct modeswitch_device {
  struct modeswitch_device* next;
  EFI_HANDLE handle;

  EFI_DEVICE_PATH_PROTOCOL*const* related_devices;

  struct ucsi_device ucsi;
  struct redriver_device redriver;

  EFI_HANDLE usb_driver; // Child node, XHCI or DWC3 device mode binds to this
  enum usb_mode mode;
  struct modeswitch_device_usb_mode modes[USB_MODE_COUNT-1];
};

extern struct modeswitch_device* modeswitch_list;

extern EFI_DEVICE_PATH_PROTOCOL* GetLastDevicePathNode(EFI_DEVICE_PATH_PROTOCOL* device_path);

extern EFI_STATUS SwitchMode(struct modeswitch_device* self, enum usb_mode mode);

#endif
