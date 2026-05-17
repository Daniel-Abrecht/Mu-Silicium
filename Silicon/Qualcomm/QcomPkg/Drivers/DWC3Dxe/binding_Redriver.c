#include "common.h"

static EFI_STATUS Redriver_Init(struct modeswitch_device* self, EFI_HANDLE ControllerHandle){
  if(self->redriver.handle)
    return EFI_ALREADY_STARTED;
  self->redriver.handle = ControllerHandle;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI Redriver_Supported(
  IN EFI_DRIVER_BINDING_PROTOCOL *binding_protocol,
  IN EFI_HANDLE ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath OPTIONAL
){
  if(RemainingDevicePath)
    return EFI_UNSUPPORTED;
  return EFI_UNSUPPORTED;
}

static EFI_STATUS EFIAPI Redriver_Stop(
  IN EFI_DRIVER_BINDING_PROTOCOL *binding_protocol,
  IN EFI_HANDLE ControllerHandle,
  IN UINTN NumberOfChildren,
  IN EFI_HANDLE *ChildHandleBuffer OPTIONAL
){
  return EFI_SUCCESS;
}


struct device_binding Redriver_binding_protocol = {
  .super = {
    .Supported = Redriver_Supported,
    .Start = Binding_Start,
    .Stop = Redriver_Stop,
    .Version = 0x200,
  },
  .Init = Redriver_Init,
};
