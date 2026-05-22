#include "common.h"

extern EFI_GUID gEdkiiNonDiscoverableDeviceProtocolGuid;

static EFI_STATUS Stop(struct modeswitch_device* self){
  if(self->mode == 0)
    return EFI_SUCCESS;
  EFI_STATUS Status = 0;
  struct modeswitch_device_usb_mode* mode = &self->modes[self->mode-1];
  if(!mode->protocol)
    return EFI_SUCCESS;
  // Note: UninstallMultipleProtocolInterfaces calls DisconnectController for any of the protocols opened by a driver
  Status = gBS->UninstallMultipleProtocolInterfaces(&self->usb_driver, mode->protocol_guid, mode->protocol, NULL);
  if(EFI_ERROR(Status))
    return Status;
  Status = gBS->CloseProtocol(
    self->handle,
    &gEfiDevicePathProtocolGuid,
    USBModeSwitch_binding_protocol.super.DriverBindingHandle,
    self->usb_driver
  );
  if(EFI_ERROR(Status))
    DEBUG((EFI_D_WARN, "CloseProtocol failed! %d\n", Status));
  self->usb_driver = NULL;
  self->mode = USB_MODE_DISCONNECTED;
  return EFI_SUCCESS;
}

static EFI_STATUS Start(struct modeswitch_device* self, enum usb_mode new_mode){
  EFI_STATUS Status = 0;
  if(new_mode < 0 || new_mode >= USB_MODE_COUNT)
    return EFI_INVALID_PARAMETER;
  if(new_mode == USB_MODE_DISCONNECTED)
    return EFI_SUCCESS;
  struct modeswitch_device_usb_mode* mode = &self->modes[new_mode-1];
  if(!mode->protocol) return EFI_SUCCESS;
  // Note: We don't actually care about the protocol we open, we just want the usb_driver handle to be a child handle of self->handle. 
  void* device_path = 0;
  Status = gBS->OpenProtocol(
    self->handle,
    &gEfiDevicePathProtocolGuid, (VOID**)&device_path,
    UCSIConnector_binding_protocol.super.DriverBindingHandle,
    self->usb_driver,
    EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
  );
  if(EFI_ERROR(Status))
    return Status;
  Status = gBS->InstallMultipleProtocolInterfaces(&self->usb_driver, mode->protocol_guid, mode->protocol, NULL);
  if(EFI_ERROR(Status)){
    gBS->CloseProtocol(
      self->handle,
      &gEfiDevicePathProtocolGuid,
      USBModeSwitch_binding_protocol.super.DriverBindingHandle,
      self->usb_driver
    );
    return Status;
  }
  self->mode = new_mode;
  gBS->ConnectController(self->usb_driver, NULL, NULL, TRUE);
  return EFI_SUCCESS;
}

EFI_STATUS SwitchMode(struct modeswitch_device* self, enum usb_mode mode){
  if(mode < 0 || mode >= USB_MODE_COUNT)
    return EFI_INVALID_PARAMETER;
  if(mode != USB_MODE_DISCONNECTED && !self->modes[mode-1].protocol)
    return EFI_UNSUPPORTED;
  if(self->mode == mode)
    return EFI_SUCCESS;
  EFI_STATUS Status = Stop(self);
  if(EFI_ERROR(Status))
    return Status;
  return Start(self, mode);
}
