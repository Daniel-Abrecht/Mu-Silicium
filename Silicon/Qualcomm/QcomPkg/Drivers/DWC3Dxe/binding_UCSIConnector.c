#include "common.h"


static EFI_STATUS UCSIConnector_Init(struct modeswitch_device* self, EFI_HANDLE ControllerHandle){
  EFI_STATUS Status;
  if(self->ucsi.handle)
    return EFI_ALREADY_STARTED;
  self->ucsi.handle = ControllerHandle;
  Status = gBS->OpenProtocol(
    ControllerHandle,
    &gUcsiConnectorOpmProtocolGuid, (VOID**)&self->ucsi.protocol,
    UCSIConnector_binding_protocol.super.DriverBindingHandle,
    ControllerHandle,
    EFI_OPEN_PROTOCOL_BY_DRIVER
  );
  if(EFI_ERROR(Status))
    return Status;
  // DEBUG((EFI_D_WARN, "UCSIConnector_Init done\n"));
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI UCSIConnector_Supported(
  IN EFI_DRIVER_BINDING_PROTOCOL *binding_protocol,
  IN EFI_HANDLE ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath OPTIONAL
){
  if(RemainingDevicePath)
    return EFI_UNSUPPORTED;
  return gBS->OpenProtocol(
    ControllerHandle,
    &gUcsiConnectorOpmProtocolGuid, NULL,
    UCSIConnector_binding_protocol.super.DriverBindingHandle,
    ControllerHandle,
    EFI_OPEN_PROTOCOL_TEST_PROTOCOL
  );
}

static EFI_STATUS EFIAPI UCSIConnector_Stop(
  IN EFI_DRIVER_BINDING_PROTOCOL *binding_protocol,
  IN EFI_HANDLE ControllerHandle,
  IN UINTN NumberOfChildren,
  IN EFI_HANDLE *ChildHandleBuffer OPTIONAL
){
  struct modeswitch_device* driver;
  for(driver=modeswitch_list; driver; driver=driver->next)
    if(driver->ucsi.handle == ControllerHandle)
      break;
  if(!driver)
    return EFI_NOT_STARTED;
  driver->handle = 0;
  gBS->CloseProtocol(
    ControllerHandle,
    &gUcsiConnectorOpmProtocolGuid,
    UCSIConnector_binding_protocol.super.DriverBindingHandle,
    ControllerHandle
  );
  return EFI_SUCCESS;
}


struct device_binding UCSIConnector_binding_protocol = {
  .super = {
    .Supported = UCSIConnector_Supported,
    .Start = Binding_Start,
    .Stop = UCSIConnector_Stop,
    .Version = 0x200,
  },
  .Init = UCSIConnector_Init,
};
