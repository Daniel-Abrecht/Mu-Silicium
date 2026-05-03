#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/DriverBinding.h>
#include "ucsi.h"

// TODO: Implement the driver binding protocol in the GLinkHelper driver and restructure it.
// Currently, we initialize the glink channel in this channel using the glink helper protocol
// after getting the channel information from the device path, but that should be done by the
// GLinkHelper instead.

extern EFI_GUID gVDP_GlinkRemoteProtocolGuid;
extern EFI_GUID gVDP_GlinkChannelProtocolGuid;
extern EFI_GUID gVDP_GlinkUcsiProtocolGuid;
extern EFI_GUID gVDP_UCSIConnectorProtocolGuid;

static EFI_HANDLE image_handle;

VOID EFIAPI ExitBootServices(IN EFI_EVENT Event, IN VOID *Context) {
  // if(this->glink) mGlinkHelperProtocol->close(this->glink);
}

void GetLastNNodes(EFI_DEVICE_PATH_PROTOCOL* device_path, int n, EFI_DEVICE_PATH_PROTOCOL* out[n]){
  EFI_DEVICE_PATH_PROTOCOL* tmp[n];
  gBS->SetMem(tmp, sizeof(EFI_DEVICE_PATH_PROTOCOL*)*n, 0);
  int i=0;
  for(EFI_DEVICE_PATH_PROTOCOL* it=device_path; !IsDevicePathEndType(it); it=NextDevicePathNode(it)){
    tmp[i] = it;
    if(++i >= n) i = 0;
  }
  for(int j=n; j--; ){
    out[j] = tmp[i];
    if(++i >= n) i = 0;
  }
}

EFI_DEVICE_PATH_PROTOCOL* GetLastDevicePathNode(EFI_DEVICE_PATH_PROTOCOL* device_path){
  EFI_DEVICE_PATH_PROTOCOL *current=device_path, *next=device_path;
  while(!IsDevicePathEndType(next)){
    current = next;
    next = NextDevicePathNode(current);
  }
  return current;
}

static EFI_STATUS EFIAPI UCSI_BindingStartSupported(
  IN EFI_DRIVER_BINDING_PROTOCOL *this,
  IN EFI_HANDLE ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath OPTIONAL,
  BOOLEAN start
){
  EFI_DEVICE_PATH_PROTOCOL* device_path = DevicePathFromHandle(ControllerHandle);
  if(!device_path)
    return EFI_UNSUPPORTED;
  EFI_DEVICE_PATH_PROTOCOL* nodes[3];
  GetLastNNodes(device_path, 3, nodes);
  if( !nodes[0] || !nodes[1] || !nodes[2]
   || nodes[0]->Type != HARDWARE_DEVICE_PATH || nodes[0]->SubType != HW_VENDOR_DP
   || nodes[1]->Type != HARDWARE_DEVICE_PATH || nodes[1]->SubType != HW_VENDOR_DP
   || nodes[2]->Type != HARDWARE_DEVICE_PATH || nodes[2]->SubType != HW_VENDOR_DP
  ) return EFI_UNSUPPORTED;
  VENDOR_DEVICE_PATH* dp_ucsi = (VENDOR_DEVICE_PATH*)nodes[0];
  VENDOR_DEVICE_PATH* dp_glink_channel = (VENDOR_DEVICE_PATH*)nodes[1];
  VENDOR_DEVICE_PATH* dp_glink_remote = (VENDOR_DEVICE_PATH*)nodes[2];
  if( !CompareGuid(&dp_ucsi->Guid, &gVDP_GlinkUcsiProtocolGuid)
   || !CompareGuid(&dp_glink_channel->Guid, &gVDP_GlinkChannelProtocolGuid)
   || !CompareGuid(&dp_glink_remote->Guid, &gVDP_GlinkRemoteProtocolGuid)
  ) return EFI_UNSUPPORTED;
  UINT16 glink_owner_id;
  gBS->CopyMem(&glink_owner_id, dp_ucsi+1, 2);
  const UINTN dp_glink_channel_length = (dp_glink_channel->Header.Length[0] | (dp_glink_channel->Header.Length[1]<<8)) - sizeof(VENDOR_DEVICE_PATH);
  const char*const dp_glink_channel_data = (const char*)(dp_glink_channel+1);
  if(dp_glink_channel_data[dp_glink_channel_length-1])
    return EFI_UNSUPPORTED;
  const UINTN dp_glink_remote_length = (dp_glink_remote->Header.Length[0] | (dp_glink_remote->Header.Length[1]<<8)) - sizeof(VENDOR_DEVICE_PATH);
  const char*const dp_glink_remote_data = (const char*)(dp_glink_remote+1);
  if(dp_glink_remote_data[dp_glink_remote_length-1] || AsciiStrLen(dp_glink_remote_data)+1 >= dp_glink_remote_length)
    return EFI_UNSUPPORTED;
  if(start){
    // static BOOLEAN initialized = FALSE; // TODO: check this properly!
    // if(initialized)
    //   return EFI_ALREADY_STARTED;
    // initialized = TRUE;
    const char*const xport   = dp_glink_remote_data;
    const char*const remote  = dp_glink_remote_data + AsciiStrLen(dp_glink_remote_data)+1;
    const char*const channel = dp_glink_channel_data;
    DEBUG((EFI_D_WARN, "UCSI_BindingSupported: %a %a %a %04X\n", xport, remote, channel, glink_owner_id));
    struct glink_ucsi* ucsi = 0;
    EFI_STATUS Status = glink_ucsi_create(&ucsi, ControllerHandle, xport, remote, channel);
    if(EFI_ERROR(Status)){
      DEBUG((EFI_D_ERROR, "glink_ucsi_create failed! Status = %r\n", Status));
      return Status;
    }
  }
  return EFI_SUCCESS;
}

void connectors_init(struct glink_ucsi* this){
  EFI_STATUS Status;
  const int connector_count = this->capability.bNumConnectors;
  Status = gBS->AllocatePool(EfiBootServicesData, sizeof(*this->connector), (VOID**)&this->connector);
  if(EFI_ERROR(Status)){
    DEBUG((EFI_D_ERROR, "glink ucsi: connectors_init: AllocatePool failed! Status = %r\n", Status));
    return;
  }
  EFI_DEVICE_PATH_PROTOCOL* device_path = DevicePathFromHandle(this->handle);
  CONTROLLER_DEVICE_PATH controller_node = {
    .Header = {
      .Type = HARDWARE_DEVICE_PATH,
      .SubType = HW_CONTROLLER_DP,
      .Length = { sizeof(CONTROLLER_DEVICE_PATH) },
    }
  };
  for(int i=0; i<connector_count; i++){
    struct ucsi_connector* connector = &this->connector[i];
    controller_node.ControllerNumber = i+1;
    if(!DevicePathFromHandle(connector->handle)){
      EFI_DEVICE_PATH_PROTOCOL* child_device_path = AppendDevicePathNode(device_path, &controller_node.Header);
      Status = gBS->InstallMultipleProtocolInterfaces(
        &connector->handle,
        &gEfiDevicePathProtocolGuid, child_device_path,
  //      &gMyCustomIoProtocolGuid,    MyIoInstance,
        NULL
      );
      if(EFI_ERROR(Status)){
        DEBUG((EFI_D_ERROR, "glink ucsi: connectors_init: InstallMultipleProtocolInterfaces failed! Status = %r\n", Status));
        return;
      }
    }

    VOID* protocol;
    Status = gBS->OpenProtocol(
      this->handle,
      &gEfiDevicePathProtocolGuid, // TODO: once we move GlinkHelper to a proper device binding interface, we'll use the protocol it provides on this handle.
      &protocol,
      image_handle,
      connector->handle,
      EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
    );
    if(EFI_ERROR(Status)){
      DEBUG((EFI_D_ERROR, "glink ucsi: connectors_init: OpenProtocol failed! Status = %r\n", Status));
      return;
    }
  }
}

void connectors_destroy(struct glink_ucsi* this){
  
}


static EFI_STATUS EFIAPI UCSI_BindingSupported(
  IN EFI_DRIVER_BINDING_PROTOCOL *this,
  IN EFI_HANDLE ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath OPTIONAL
){
  return UCSI_BindingStartSupported(this, ControllerHandle, RemainingDevicePath, FALSE);
}

static EFI_STATUS EFIAPI UCSI_BindingStart(
  IN EFI_DRIVER_BINDING_PROTOCOL *this,
  IN EFI_HANDLE ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath OPTIONAL
){
  return UCSI_BindingStartSupported(this, ControllerHandle, RemainingDevicePath, TRUE);
}

static EFI_STATUS EFIAPI UCSI_BindingStop(
  IN EFI_DRIVER_BINDING_PROTOCOL *this,
  IN EFI_HANDLE ControllerHandle,
  IN UINTN NumberOfChildren,
  IN EFI_HANDLE *ChildHandleBuffer OPTIONAL
){
  // TODO
  return EFI_SUCCESS;
}

// ControllerHandleList is a null terminated list of all the handles we have installed the EFI_DRIVER_BINDING_PROTOCOL on.
void ProbeAllDevicePaths(EFI_HANDLE* ControllerHandleList){
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
  DEBUG ((EFI_D_WARN, "ucsi_init\n"));
  EFI_STATUS Status;
  image_handle = ImageHandle;

  Status = gBS->LocateProtocol (&gGlinkHelperProtocolGuid, NULL, (VOID *)&mGlinkHelperProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate GlinkHelper Protocol! Status = %r\n", Status));
    goto error;
  }

  {
    static EFI_EVENT ExitEvt;
    Status = gBS->CreateEvent(EVT_SIGNAL_EXIT_BOOT_SERVICES, TPL_NOTIFY, ExitBootServices, NULL, &ExitEvt);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "CreateEvent for EVT_SIGNAL_EXIT_BOOT_SERVICES failed! Status = %r\n", Status));
      goto error;
    }
  }

  static EFI_DRIVER_BINDING_PROTOCOL ucsi_binding_protocol = {
    .Supported = UCSI_BindingSupported,
    .Start = UCSI_BindingStart,
    .Stop = UCSI_BindingStop,
    .Version = 0x1000,
  };
  ucsi_binding_protocol.ImageHandle = ImageHandle;
  ucsi_binding_protocol.DriverBindingHandle = ImageHandle;

  Status = gBS->InstallMultipleProtocolInterfaces(
    &ImageHandle,
    &gEfiDriverBindingProtocolGuid, &ucsi_binding_protocol,
    NULL
  );
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Install ULog Protocol! Status = %r\n", Status));
    Status = -1;
    goto error;
  }

  ProbeAllDevicePaths((EFI_HANDLE[]){ucsi_binding_protocol.DriverBindingHandle, 0});
  DEBUG ((EFI_D_WARN, "ucsi_init done!\n"));

  return EFI_SUCCESS;
error:
  return EFI_DEVICE_ERROR;
}
