#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include "pmic_typec.h"
#include "EFISPMI.h"

EFI_QCOM_SPMI_PROTOCOL *mQcomSPMIProtocol;

/*
STATIC VOID EFIAPI Poll(IN EFI_EVENT Event, IN VOID *Context)
{
  // EFI_STATUS Status;

  {
    UINT8 data[1] = {0};
    UINT32 len = 1;
    Spmi_Result result = mQcomSPMIProtocol->ReadLong(mQcomSPMIProtocol,
                           0, 7, SPMI_ACCESS_PRIORITY_HIGH, 0x900 + 0x2200 + TYPEC_MODE_CFG_REG,
                           data, len, &len);
    if(result || len != 1){
      DEBUG ((EFI_D_ERROR, "QcomSPMIProtocol->ReadLong failed! Status = %d\n", result));
      goto error;
    }
    DEBUG((EFI_D_WARN, "TYPEC regs TYPEC_MODE_CFG_REG (0x44): %02X\n", data[0], len));
  }

  {
    // 0: TYPEC_SNK_STATUS_REG = 6
    // 1: TYPEC_DEBUG_ACCESS_STATUS = 7
    // 2: TYPEC_SRC_STATUS_REG = 8
    // 3: TYPEC_STATE_MACHINE_STATUS_REG = 9
    // 4: TYPEC_SM_STATUS_REG = A
    // 5: TYPEC_MISC_STATUS_REG = B
    UINT8 data[6] = {0};
    UINT32 len = 6;
    Spmi_Result result = mQcomSPMIProtocol->ReadLong(mQcomSPMIProtocol,
                           0, 7, SPMI_ACCESS_PRIORITY_HIGH, 0x900 + 0x2200 + TYPEC_SNK_STATUS_REG,
                           data, len, &len);
    if(result || len != 6){
      DEBUG ((EFI_D_ERROR, "QcomSPMIProtocol->ReadLong failed! Status = %d\n", result));
      goto error;
    }
    DEBUG((EFI_D_WARN, "TYPEC regs 6..B: %02X  %02X  %02X  %02X  %02X  %02X\n", data[0], data[1], data[2], data[3], data[4], data[5], len));
  }

error:;
}
*/

EFI_STATUS EFIAPI Main(
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS             Status;

  DEBUG((EFI_D_WARN, "\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n\n"));
  
  // Locate Display Power State Protocol
  Status = gBS->LocateProtocol (&gQcomSPMIProtocolGuid, NULL, (VOID *)&mQcomSPMIProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate Qcom SPMI Protocol! Status = %r\n", Status));
    goto error;
  }

  {
    UINT8 data[1] = {EN_TRY_SNK};
    UINT32 len = 1;
    Spmi_Result result = mQcomSPMIProtocol->WriteLong(mQcomSPMIProtocol,
                            0, 7, SPMI_ACCESS_PRIORITY_HIGH, 0x900 + 0x2200 + TYPEC_MODE_CFG_REG,
                            data, len);
    if(result || len != 1){
      DEBUG ((EFI_D_ERROR, "QcomSPMIProtocol->WriteLong failed! Status = %d\n", result));
      goto error;
    }
  }

/*
  static EFI_EVENT PollEvt;
  Status = gBS->CreateEvent(
    EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
    Poll, NULL, &PollEvt
  );
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to create TypeCHelper poll Event! Status = %r\n", Status));
    goto error;
  }

  Status = gBS->SetTimer(PollEvt, TimerPeriodic, 1000000);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "TypeCHelper SetTimer failed! Status = %r\n", Status));
    goto error;
  }
  gBS->SignalEvent(PollEvt);
*/

  DEBUG((EFI_D_WARN, "\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n\n"));
  gBS->Stall(1000000);
  return EFI_SUCCESS;

error:
  DEBUG((EFI_D_WARN, "\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n\n"));
  gBS->Stall(1000000);
  return Status;
}
