#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/ULogCtl.h>

#include "pmic_typec.h"
#include "EFISPMI.h"

//#include "firmware/adsp.h"

extern EFI_GUID gEfiPilProtocolGuid;
extern EFI_GUID gPmicGlinkProtocolGuid;
extern EFI_GUID gULogCtlProtocolGuid;
EFI_QCOM_SPMI_PROTOCOL *mQcomSPMIProtocol;

#define UCSI_CMD_SET_CCOM 0x08
#define UCSI_CMD_SET_UOR  0x09
#define UCSI_CMD_SET_PDM  0x0A
#define UCSI_CMD_SET_PDR  0x0B

typedef struct _EFI_PM_GLINK_LINK_STATUS
{
  BOOLEAN IsPMICChannelOpen;
  /* Add more when needed */
}EFI_PM_GLINK_LINK_STATUS;

typedef EFI_STATUS(EFIAPI *EFI_PMIC_GLINK_UCSI_READ_BUFFER)(OUT UINT8** ppReadBuffer, IN UINT8 size);
typedef EFI_STATUS(EFIAPI *EFI_PMIC_GLINK_UCSI_WRITE_BUFFER)(IN UINT8* pWriteBuffer, IN UINT8 size);
typedef EFI_STATUS(EFIAPI *EFI_PMIC_GLINK_USBC_READ_BUFFER)(IN UINT8** ppReadBuffer, IN UINT8 size);
typedef EFI_STATUS(EFIAPI *EFI_PMIC_GLINK_USBC_WRITE_BUFFER)(IN  UINT8* pWriteBuffer, IN  UINT8 size );
typedef EFI_STATUS(EFIAPI *EFI_PMIC_GLINK_CONNECT)(void);
typedef EFI_STATUS(EFIAPI *EFI_PMIC_GLINK_LINK_STATUS)(OUT EFI_PM_GLINK_LINK_STATUS *PmicGLinkStatus);

struct _EFI_PMIC_GLINK_PROTOCOL
{
    UINT64                                    Revision;
    EFI_PMIC_GLINK_UCSI_READ_BUFFER           UCSIReadBuffer;         /*Read ucsi data buffer from adsp*/
    EFI_PMIC_GLINK_UCSI_WRITE_BUFFER          UCSIWriteBuffer;        /*write ucsi data buffer to adsp*/
    EFI_PMIC_GLINK_USBC_READ_BUFFER           USBCReadBuffer;         /*Read usbc data buffer from adsp*/
    EFI_PMIC_GLINK_USBC_WRITE_BUFFER          USBCWriteBuffer;        /*Write usbc data buffer to adsp*/                        
    void*                                     GetBatteryCount;        /* Get number of batteries*/
    void*                                     GetBatteryStatus;       /*Get Battery voltage, current, percentage, temperature, etc.*/
    void*                                     IsBatteryPresent;       /*indicate if battery is present or not*/
    EFI_PMIC_GLINK_CONNECT                    Connect;                /* GLink Open */
    EFI_PMIC_GLINK_LINK_STATUS                LinkStatus;             /*Link Status */
};
typedef struct _EFI_PMIC_GLINK_PROTOCOL EFI_PMIC_GLINK_PROTOCOL;

typedef struct _EFI_PIL_PROTOCOL {
  UINT64 Revision;
  EFI_STATUS (EFIAPI *ProcessPilImage) (IN CHAR16* Subsys);
} EFI_PIL_PROTOCOL;


/*STATIC VOID EFIAPI Poll(IN EFI_EVENT Event, IN VOID *Context)
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
}*/


#define CCI_BIT_end_of_message_indicator(CCI)   (1<<0)
#define CCI_get_connector_change_indicator(CCI) (((CCI)>>1)&0x7F)
#define CCI_get_data_length(CCI)                (((CCI)>>8)&0xFF)
#define CCI_BIT_security_request    (1<<23)
#define CCI_BIT_fw_update_request   (1<<24)
#define CCI_BIT_not_supported       (1<<25)
#define CCI_BIT_cancel_completed    (1<<26)
#define CCI_BIT_reset_completed     (1<<27)
#define CCI_BIT_busy                (1<<28)
#define CCI_BIT_acknowledge_command (1<<29)
#define CCI_BIT_error               (1<<30)
#define CCI_BIT_command_completed   (1<<31)

// UCSIReadBuffer / UCSIWriteBuffer wwill actually always read / write exactly 48 bytes.
// USBCReadBuffer / USBWriteBuffer wwill actually always read / write exactly 16 / 8 bytes respectivly.
// They error if the size parameter is less than that, but don't care what value it is otherwise...
struct ucsi_command_data {
  UINT16 version; // 8 major, 4 minor, 4 patch
  UINT16 reserved;
  UINT32 cci;
  struct {
    UINT8 command;
    UINT8 data_length;
    UINT8 cmdfield[6]; // command speciffic fields
  } control;
//  UINT8 message_in [256];
//  UINT8 message_out[256];
  UINT8 padding[32];
};
_Static_assert(sizeof(struct ucsi_command_data) == 48, "NOOOOOOOOOOO!!!");


EFI_STATUS EFIAPI Main(
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS             Status;

  DEBUG((EFI_D_WARN, "\n\n\n\n\n\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n\n"));

  static EFI_ULOG_CTL_PROTOCOL* mULogCtl;
  Status = gBS->LocateProtocol (&gULogCtlProtocolGuid, NULL, (VOID *)&mULogCtl);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate ULogCtl Protocol! Status = %r\n", Status));
    goto error;
  }
  mULogCtl->EnableLog(ULOGCTL_ANY_LOG, 1);

  Status = gBS->LocateProtocol (&gQcomSPMIProtocolGuid, NULL, (VOID *)&mQcomSPMIProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate Qcom SPMI Protocol! Status = %r\n", Status));
    goto error;
  }

/*  static EFI_EVENT PollEvt;
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
  gBS->SignalEvent(PollEvt);*/

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

  EFI_PIL_PROTOCOL* mPILProtocol = 0;
  Status = gBS->LocateProtocol(&gEfiPilProtocolGuid, NULL, (VOID **)&mPILProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate PIL Protocol! Status = %r\n", Status));
    goto error;
  }

/*  EFI_PMIC_GLINK_PROTOCOL* mPmicGlinkProtocol;
  Status = gBS->LocateProtocol (&gPmicGlinkProtocolGuid, NULL, (VOID *)&mPmicGlinkProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate Qcom PMIC glink Protocol! Status = %r\n", Status));
    goto error;
  }*/

  DEBUG ((EFI_D_WARN, "PILProtocol->ProcessPilImage\n"));

  // Should we check the link status before loading the adsp firmware in case the charger library has already loeded it?
  Status = mPILProtocol->ProcessPilImage(L"FULL_ADSP");
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "ProcessPilImage Failed! Status = %r\n", Status));
  }

  gBS->Stall(7*1000000);
  /*DEBUG ((EFI_D_WARN, "mPmicGlinkProtocol->Connect\n"));

  {
    EFI_STATUS Status = mPmicGlinkProtocol->Connect();
    if (EFI_ERROR(Status))
    {
      DEBUG((EFI_D_ERROR, "Connect Failed %r\r\n", Status));
      // goto error;
    }
  }

  DEBUG ((EFI_D_WARN, "mPmicGlinkProtocol->LinkStatus\n"));

  {
    EFI_PM_GLINK_LINK_STATUS ls = {0};
    EFI_STATUS Status = mPmicGlinkProtocol->LinkStatus(&ls);
    if (EFI_ERROR(Status))
    {
      DEBUG((EFI_D_ERROR, "LinkStatus Failed %r\r\n", Status));
      // goto error;
    }else{
      DEBUG((EFI_D_ERROR, "mPmicGlinkProtocol->LinkStatus: %d\r\n", ls.IsPMICChannelOpen));
    }
  }

  DEBUG ((EFI_D_WARN, "mPmicGlinkProtocol->UCSIWriteBuffer GET_CONNECTOR_STATUS\n"));

  {
    struct ucsi_command_data ucsi_cmd = {
      .control = {
        .command = 0x12,
        .cmdfield[0] = 1, // 7 bits, Connector Number, 1 bit swap to dfp
      }
    };

    Status = mPmicGlinkProtocol->UCSIWriteBuffer((UINT8*)&ucsi_cmd, 48);
    if (EFI_ERROR(Status))
    {
      DEBUG((EFI_D_ERROR, "GET_CONNECTOR_STATUS Failed %r\r\n", Status));
      // goto error;
    }
  }

  DEBUG ((EFI_D_WARN, "mPmicGlinkProtocol->UCSIWriteBuffer SET_CCOM\n"));

  {
    struct ucsi_command_data ucsi_cmd = {
      .control = {
        .command = UCSI_CMD_SET_CCOM,
        .cmdfield[0] = 1, // 7 bits, Connector Number, 1 bit Rp Only
        .cmdfield[1] = 2, // DRP mode (1bit Rd Only, 1bit DRP)
      }
    };

    Status = mPmicGlinkProtocol->UCSIWriteBuffer((UINT8*)&ucsi_cmd, 48);
    if (EFI_ERROR(Status))
    {
      DEBUG((EFI_D_ERROR, "UCSI_CMD_SET_CCOM Failed\r\n"));
      // goto error;
    }
  }

  DEBUG ((EFI_D_WARN, "mPmicGlinkProtocol->UCSIWriteBuffer SET_PDR\n"));

  {
    struct ucsi_command_data ucsi_cmd = {
      .control = {
        .command = UCSI_CMD_SET_PDR,
        .cmdfield[0] = 1, // 7 bits, Connector Number, 1bit swap to source
        .cmdfield[1] = 2, // DRP mode (1bit swap to sink, 1bit accept power role swap)
      }
    };

    Status = mPmicGlinkProtocol->UCSIWriteBuffer((UINT8*)&ucsi_cmd, 48);
    if (EFI_ERROR(Status))
    {
      DEBUG((EFI_D_ERROR, "UCSI_CMD_SET_PDR Failed\r\n"));
      // goto error;
    }
  }

  DEBUG ((EFI_D_WARN, "mPmicGlinkProtocol->UCSIWriteBuffer SET_UOR\n"));

  {
    struct ucsi_command_data ucsi_cmd = {
      .control = {
        .command = UCSI_CMD_SET_UOR,
        .cmdfield[0] = 1, // 7 bits, Connector Number, 1 bit swap to dfp
        .cmdfield[1] = 2, // DPR mode (1bit swap to ufp, 1bit accept role swap requests)
      }
    };

    Status = mPmicGlinkProtocol->UCSIWriteBuffer((UINT8*)&ucsi_cmd, 48);
    if (EFI_ERROR(Status))
    {
      DEBUG((EFI_D_ERROR, "UCSI_CMD_SET_UOR Failed\r\n"));
      // goto error;
    }
  }*/

/*  
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
*/
  DEBUG((EFI_D_WARN, "Waiting for TYPEC regs TYPEC_MODE_CFG_REG (0x44) to be set to 0x10 by the adsp\n"));
  while(1){
    UINT8 data[1] = {0};
    UINT32 len = 1;
    Spmi_Result result = mQcomSPMIProtocol->ReadLong(mQcomSPMIProtocol,
                           0, 7, SPMI_ACCESS_PRIORITY_HIGH, 0x900 + 0x2200 + TYPEC_MODE_CFG_REG,
                           data, len, &len);
    if(result || len != 1){
      DEBUG ((EFI_D_ERROR, "QcomSPMIProtocol->ReadLong failed! Status = %d\n", result));
      goto error;
    }
    // DEBUG((EFI_D_WARN, "TYPEC regs TYPEC_MODE_CFG_REG (0x44): %02X\n", data[0], len));
    if(data[0] == 0x10)
      break;

    gBS->Stall(100000);
  }

  mULogCtl->EnableLog(ULOGCTL_ANY_LOG, 0);
  DEBUG((EFI_D_WARN, "\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n\n"));
  //gBS->Stall(5*1000000);
  return EFI_SUCCESS;

error:
  mULogCtl->EnableLog(ULOGCTL_ANY_LOG, 0);
  DEBUG((EFI_D_WARN, "\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n\n"));
  gBS->Stall(5*1000000);
  return Status;
}
