#include "common.h"

struct modeswitch_device* modeswitch_list;

// ControllerHandleList is a null terminated list of all the handles we have installed the EFI_DRIVER_BINDING_PROTOCOL on.
static void ProbeAllDevicePaths(EFI_HANDLE* ControllerHandleList){
  EFI_STATUS Status;
  UINTN HandleCount;
  EFI_HANDLE *HandleBuffer;

  Status = gBS->LocateHandleBuffer(
    ByProtocol, &gEfiDevicePathProtocolGuid, NULL,
    &HandleCount, &HandleBuffer
  );
  if(EFI_ERROR(Status))
    return;
  for(UINTN i=0; i < HandleCount; i++){
    Status = gBS->ConnectController(HandleBuffer[i], ControllerHandleList, NULL, TRUE);
  }
  gBS->FreePool(HandleBuffer);
}


EFI_STATUS EFIAPI Main(
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
){
  EFI_STATUS Status = 0;

  // Main device, for the other bindings, new handles are genenrated
  USBModeSwitch_binding_protocol.super.DriverBindingHandle = ImageHandle;

  for(struct device_binding*const* it=binding_protocol_list; *it; it++){
    struct device_binding* binding_protocol = *it;
    binding_protocol->super.ImageHandle = ImageHandle;
    Status = gBS->InstallMultipleProtocolInterfaces(
      &binding_protocol->super.DriverBindingHandle,
      &gEfiDriverBindingProtocolGuid, &binding_protocol->super,
      NULL
    );
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "Failed to install driver binding protocol! Status = %r\n", Status));
      goto error;
    }    
  }

  void phy_init_test(void);
  phy_init_test();

  ProbeAllDevicePaths((EFI_HANDLE[]){USBModeSwitch_binding_protocol.super.DriverBindingHandle, 0});

  return EFI_SUCCESS;
error:
  return Status;
}
