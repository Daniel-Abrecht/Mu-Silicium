#include "common.h"


static EFI_STATUS insert_device(struct modeswitch_device** pit, struct modeswitch_device* dev){
  EFI_TPL OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
  for(; *pit; pit=&(*pit)->next){
    struct modeswitch_device* it = *pit;
    if(it->handle == dev->handle){
      gBS->RestoreTPL(OldTpl);
      return EFI_ALREADY_STARTED;
    }
    if(it->handle > dev->handle)
      break;
  }
  dev->next = *pit;
  *pit = dev;
  gBS->RestoreTPL(OldTpl);
  return EFI_SUCCESS;
}

static EFI_STATUS USBModeSwitch_Init(struct modeswitch_device* self, EFI_HANDLE ControllerHandle){
  EFI_STATUS Status = 0;
  self->handle = ControllerHandle;
  Status = insert_device(&modeswitch_list, self);
  if(EFI_ERROR(Status))
    return Status;

  Status = gBS->OpenProtocol(
    ControllerHandle,
    &gEfiDependentDevices, (VOID**)&self->related_devices,
    USBModeSwitch_binding_protocol.super.DriverBindingHandle,
    ControllerHandle,
    EFI_OPEN_PROTOCOL_GET_PROTOCOL
  );
  if(EFI_ERROR(Status) && Status != EFI_UNSUPPORTED)
    return Status;

  for(EFI_DEVICE_PATH_PROTOCOL*const* it=self->related_devices; *it; it++){
    EFI_DEVICE_PATH_PROTOCOL* result = *it;
    EFI_HANDLE handle = 0;
    Status = gBS->LocateDevicePath(&gEfiDevicePathProtocolGuid, &result, &handle);
    if(EFI_ERROR(Status) || !IsDevicePathEnd(result))
      continue;
    for(struct device_binding*const* it=binding_protocol_list+1; *it; it++){
      struct device_binding* binding_protocol = *it;
      if(EFI_ERROR(binding_protocol->super.Supported(&binding_protocol->super, handle, NULL)))
        continue;
      Status = binding_protocol->Init(self, handle);
      if(EFI_ERROR(Status)){
        DEBUG((EFI_D_WARN, "binding_protocol->Init %r\n", Status));
        continue;
      }
      break;
    }
  }

  // DEBUG((EFI_D_WARN, "USBModeSwitch_Init done\n"));
  // gBS->Stall(10000000);
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI USBModeSwitch_SupportedStart(
  IN EFI_DRIVER_BINDING_PROTOCOL *binding_protocol,
  IN EFI_HANDLE ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath OPTIONAL,
  BOOLEAN do_start
){
  EFI_STATUS Status = 0;
  if(RemainingDevicePath)
    return EFI_UNSUPPORTED;
  EFI_DEVICE_PATH_PROTOCOL* device_path = DevicePathFromHandle(ControllerHandle);
  if(!device_path)
    return EFI_UNSUPPORTED;
  EFI_DEVICE_PATH_PROTOCOL* dp = GetLastDevicePathNode(device_path);
  if(dp->Type != HARDWARE_DEVICE_PATH || dp->SubType != HW_VENDOR_DP)
    return EFI_UNSUPPORTED;
  VENDOR_DEVICE_PATH* vdp = (VENDOR_DEVICE_PATH*)dp;
  if(!CompareGuid(&vdp->Guid, &gVDP_DWC3ControllerProtocolGuid))
    return EFI_UNSUPPORTED;

  if(!do_start)
    return EFI_SUCCESS;

  struct modeswitch_device* self;
  Status = gBS->AllocatePool(EfiBootServicesData, sizeof(*self), (VOID**)&self);
  if(EFI_ERROR(Status)){
    DEBUG((EFI_D_ERROR, "DWC3Dxe: USBModeSwitch_SupportedStart: AllocatePool failed! Status = %r\n", Status));
    return Status;
  }
  gBS->SetMem(self, sizeof(*self), 0);

  Status = USBModeSwitch_Init(self, ControllerHandle);
  if(EFI_ERROR(Status))
    gBS->FreePool(self);
  return Status;
}

static EFI_STATUS EFIAPI USBModeSwitch_Supported(
  IN EFI_DRIVER_BINDING_PROTOCOL *binding_protocol,
  IN EFI_HANDLE ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath OPTIONAL
){
  return USBModeSwitch_SupportedStart(binding_protocol, ControllerHandle, RemainingDevicePath, FALSE);
}

static EFI_STATUS EFIAPI USBModeSwitch_Start(
  IN EFI_DRIVER_BINDING_PROTOCOL *binding_protocol,
  IN EFI_HANDLE ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath OPTIONAL
){
  return USBModeSwitch_SupportedStart(binding_protocol, ControllerHandle, RemainingDevicePath, TRUE);
}

static EFI_STATUS EFIAPI USBModeSwitch_Stop(
  IN EFI_DRIVER_BINDING_PROTOCOL *binding_protocol,
  IN EFI_HANDLE ControllerHandle,
  IN UINTN NumberOfChildren,
  IN EFI_HANDLE *ChildHandleBuffer OPTIONAL
){
  return EFI_SUCCESS;
}

struct device_binding USBModeSwitch_binding_protocol = {
  .super = {
    .Supported = USBModeSwitch_Supported,
    .Start = USBModeSwitch_Start,
    .Stop = USBModeSwitch_Stop,
    .Version = 0x100,
  },
  .Init = USBModeSwitch_Init,
};
