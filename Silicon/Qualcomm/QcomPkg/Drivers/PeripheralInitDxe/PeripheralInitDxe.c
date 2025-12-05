#include <Library/PcdLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>

STATIC
VOID
DummyNotify (
  IN EFI_EVENT Event,
  IN VOID     *Context) 
{
  // Dummy Function Needed for Event Notification Callback
}

STATIC
VOID
UsbInitDoneCb (
  IN EFI_EVENT Event,
  IN VOID     *Context) 
{
  // EFI_STATUS Status;
  // EFI_EVENT  ToggleEvent;
  // DEBUG ((EFI_D_WARN, "\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n\n"));
  // Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, DummyNotify, NULL, &gEfiEventToggleUsbModeGuid, &ToggleEvent);
  // if (EFI_ERROR (Status)) {
  //   DEBUG ((EFI_D_ERROR, "Failed to Create USB Mode Toggle Event! Status = %r\n", Status));
  // } else {
  //   gBS->SignalEvent (ToggleEvent);
  //   gBS->CloseEvent  (ToggleEvent);
  // }
}

EFI_STATUS
EFIAPI
InitPeripherals (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  EFI_EVENT  InitEvent;

  // Start the USB Port Controller
  Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, UsbInitDoneCb, NULL, &gUsbControllerInitGuid, &InitEvent);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Create USB Port Controller Start Event! Status = %r\n", Status));
  } else {
    gBS->SignalEvent (InitEvent);
    gBS->CloseEvent  (InitEvent);
  }

  // Init SD Card Slot
  if (FixedPcdGetBool (PcdInitCardSlot)) {
    Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, DummyNotify, NULL, &gSDCardInitGuid, &InitEvent);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "Failed to Create SD Card Init Event! Status = %r\n", Status));
    } else {
      gBS->SignalEvent (InitEvent);
      gBS->CloseEvent  (InitEvent);
    }
  }

  return Status;
}
