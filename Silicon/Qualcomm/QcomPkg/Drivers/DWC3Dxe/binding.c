#include "common.h"

struct device_binding*const binding_protocol_list[] = {
  &USBModeSwitch_binding_protocol,
  &UCSIConnector_binding_protocol,
  &Redriver_binding_protocol,
  NULL,
};

BOOLEAN CompareDevicePaths(EFI_DEVICE_PATH_PROTOCOL* a, EFI_DEVICE_PATH_PROTOCOL* b){
  UINTN size = GetDevicePathSize(a);
  if(size != GetDevicePathSize(b))
    return FALSE;
  return CompareMem(a, b, size) == 0;
}

EFI_DEVICE_PATH_PROTOCOL* GetLastDevicePathNode(EFI_DEVICE_PATH_PROTOCOL* device_path){
  if(!device_path) return 0;
  EFI_DEVICE_PATH_PROTOCOL *current=device_path, *next=device_path;
  while(!IsDevicePathEndType(next)){
    current = next;
    next = NextDevicePathNode(current);
  }
  return current;
}

EFI_STATUS EFIAPI Binding_Start(
  IN EFI_DRIVER_BINDING_PROTOCOL *binding_protocol_super,
  IN EFI_HANDLE ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath OPTIONAL
){
  struct device_binding* binding_protocol = BASE_CR(binding_protocol_super, struct device_binding, super);
  EFI_DEVICE_PATH_PROTOCOL* device_path = DevicePathFromHandle(ControllerHandle);
  for(struct modeswitch_device* driver=modeswitch_list; driver; driver=driver->next)
    for(EFI_DEVICE_PATH_PROTOCOL*const* dp=driver->related_devices; *dp; dp++)
      if(CompareDevicePaths(device_path, *dp))
        return binding_protocol->Init(driver, ControllerHandle);
  return EFI_DEVICE_ERROR;
}
