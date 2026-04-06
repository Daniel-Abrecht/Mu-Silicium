#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/ULogCtl.h>

#include "pmic_typec.h"
#include "EFISPMI.h"
#include "glink-helper.h"



//#include "firmware/adsp.h"

extern EFI_GUID gEfiPilProtocolGuid;
extern EFI_GUID gPmicGlinkProtocolGuid;
extern EFI_GUID gULogCtlProtocolGuid;
static EFI_QCOM_SPMI_PROTOCOL *mQcomSPMIProtocol;

extern EFI_GUID gGlinkHelperProtocolGuid;
static GLINK_HELPER_PROTOCOL* mGlinkHelperProtocol;

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


STATIC VOID EFIAPI Poll(IN EFI_EVENT Event, IN VOID *Context)
{
  // EFI_STATUS Status;

  {
    UINT8 cfg = {0};
    UINT8 vconn = {0};
    UINT8 ccout = {0};
    UINT8 sensor = {0};
    UINT8 estate = {0};
    UINT8 currsrc = {0};
    UINT8 water[4] = {0};
    {
      Spmi_Result result = 0;
      result |= mQcomSPMIProtocol->ReadLong(mQcomSPMIProtocol,
                  0, 7, SPMI_ACCESS_PRIORITY_HIGH, 0x900 + 0x2200 + TYPEC_MODE_CFG_REG,
                  &cfg, 1, &(UINT32){1});
      result |= mQcomSPMIProtocol->ReadLong(mQcomSPMIProtocol,
                  0, 7, SPMI_ACCESS_PRIORITY_HIGH, 0x900 + 0x2200 + TYPEC_VCONN_CONTROL_REG,
                  &vconn, 1, &(UINT32){1});
      result |= mQcomSPMIProtocol->ReadLong(mQcomSPMIProtocol,
                  0, 7, SPMI_ACCESS_PRIORITY_HIGH, 0x900 + 0x2200 + TYPEC_CCOUT_CONTROL_REG,
                  &ccout, 1, &(UINT32){1});
      result |= mQcomSPMIProtocol->ReadLong(mQcomSPMIProtocol,
                  0, 7, SPMI_ACCESS_PRIORITY_HIGH, 0x900 + 0x2200 + TYPE_C_CRUDE_SENSOR_CFG_REG,
                  &sensor, 1, &(UINT32){1});
      result |= mQcomSPMIProtocol->ReadLong(mQcomSPMIProtocol,
                  0, 7, SPMI_ACCESS_PRIORITY_HIGH, 0x900 + 0x2200 + TYPEC_EXIT_STATE_CFG_REG,
                  &estate, 1, &(UINT32){1});
      result |= mQcomSPMIProtocol->ReadLong(mQcomSPMIProtocol,
                  0, 7, SPMI_ACCESS_PRIORITY_HIGH, 0x900 + 0x2200 + TYPEC_CURRSRC_CFG_REG,
                  &currsrc, 1, &(UINT32){1});
      result |= mQcomSPMIProtocol->ReadLong(mQcomSPMIProtocol,
                  0, 7, SPMI_ACCESS_PRIORITY_HIGH, 0x900 + 0x2200 + TYPEC_U_USB_CFG_REG,
                  water, 4, &(UINT32){4});
      if(result){
        DEBUG ((EFI_D_ERROR, "QcomSPMIProtocol->ReadLong failed! Status = %d\n", result));
        goto error;
      }
    }
    DEBUG((EFI_D_WARN, "TYPEC regs: cfg:%02X vconn:%02X ccout:%02X sensor:%02X estate:%02X currsrc:%02X water:%02X %02X %02X %02X\n",
           cfg, vconn, ccout, sensor, estate, currsrc, water[0], water[1], water[2], water[3]));
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

    DEBUG((EFI_D_WARN,
      "%a%a%a%a%a%a%a%a%a%a"
      " %02X"
      "%a%a%a%a%a%a%a"
      " %02X"
      "%a%a%a%a"
      "%a%a%a%a%a%a%a%a\n",
      
      ((data[0] & DETECTED_SNK_TYPE_MASK) ? " SNK" : ""),
      ((data[0] & SNK_RP_MASK) ? ";RP" : ""),
        ((data[0] & SNK_RP_SHORT) ? ",short" : ""),
        ((data[0] & SNK_RP_3P0) ? ",3p0" : ""),
        ((data[0] & SNK_RP_1P5) ? ",1p5" : ""),
        ((data[0] & SNK_RP_STD) ? ",std" : ""),
      ((data[0] & SNK_DAM_MASK) ? ";DAM" : ""),
        ((data[0] & SNK_DAM_3000MA) ? ",3000mA" : ""),
        ((data[0] & SNK_DAM_1500MA) ? ",1500mA" : ""),
        ((data[0] & SNK_DAM_500MA) ? ",500mA" : ""),

      data[1],

      ((data[2] & DETECTED_SRC_TYPE_MASK) ? " SRC" : ""),
        ((data[2] & AUDIO_ACCESS_RA_RA) ? ",audio" : ""),
        ((data[2] & SRC_RA_OPEN) ? ",RA_open" : ""),
        ((data[2] & SRC_RD_RA_VCONN) ? ",RD_RA_VCONN" : ""),
        ((data[2] & SRC_RD_OPEN) ? ",RD_open" : ""),
        ((data[2] & SRC_DEBUG_ACCESS) ? ",debug" : ""),
        ((data[2] & SRC_HIGH_BATT) ? ",audio" : ""),

      data[3],

      ((data[4] & 0xE0) ? " SM" : ""),
        ((data[4] & TYPEC_SM_VBUS_VSAFE5V) ? ":VBUS_5V" : ""),
        ((data[4] & TYPEC_SM_VBUS_VSAFE0V) ? ":VBUS_0V" : ""),
        ((data[4] & TYPEC_SM_USBIN_LT_LV ) ? ":USBIN_LT_LV" : ""),

      " MISC",
        ((data[5] & CC_ATTACHED) ? ":CC_attached" : ""),
        ((data[5] & CC_ORIENTATION) ? ":CC_orientation" : ""),
        ((data[5] & TYPEC_DEBOUNCE_DONE) ? ":typec_debounce_done" : ""),
        ((data[5] & TYPEC_VBUS_ERROR_STATUS) ? ":typec_vbus_error" : ""),
        ((data[5] & TYPEC_VBUS_DETECT) ? ":typec_vbus_detect" : ""),
        ((data[5] & SNK_SRC_MODE) ? ":src" : ":snk"),
        ((data[5] & TYPEC_WATER_DETECTION_STATUS) ? ":water" : "")

    ));

  }

error:;
}


EFI_STATUS EFIAPI glink_helper_init(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable);

enum usb_property_id {
  USB_ONLINE = 0,
  USB_VOLT_NOW = 1,
  USB_VOLT_MAX = 2,
  USB_CURR_NOW = 3,
  USB_CURR_MAX = 4,
  USB_INPUT_CURR_LIMIT = 5,
  USB_TYPE = 6,
  USB_ADAP_TYPE = 7,
  USB_MOISTURE_DET_EN = 8,
  USB_MOISTURE_DET_STS = 9,
  USB_TEMP = 10,
  USB_REAL_TYPE = 11,
  USB_TYPEC_COMPLIANT = 12,
  USB_ADAP_SUBTYPE = 13,
  USB_VBUS_COLLAPSE_STATUS = 14,
  USB_VOOCPHY_STATUS = 15,
  USB_VOOCPHY_ENABLE = 16,
  USB_OTG_AP_ENABLE = 17,
  USB_OTG_SWITCH = 18,
  USB_POWER_SUPPLY_RELEASE_FIXED_FREQUENCE = 19,
  USB_TYPEC_CC_ORIENTATION = 20,
  USB_CID_STATUS = 21,
  USB_TYPEC_MODE = 22,
  USB_TYPEC_SINKONLY = 23,
  USB_OTG_VBUS_REGULATOR_ENABLE = 24,
  USB_VOOC_CHG_PARAM_INFO = 25,
  USB_VOOC_FAST_CHG_TYPE = 26,
  USB_DEBUG_REG = 27,
  USB_VOOCPHY_RESET_AGAIN = 28,
  USB_SUSPEND_PMIC = 29,
  USB_OEM_MISC_CTL = 30,
  USB_CCDETECT_HAPPENED = 31,
  USB_GET_PPS_TYPE = 32,
  USB_GET_PPS_STATUS = 33,
  USB_SET_PPS_VOLT = 34,
  USB_SET_PPS_CURR = 35,
  USB_GET_PPS_MAX_CURR = 36,
  USB_PPS_READ_VBAT0_VOLT = 37,
  USB_PPS_CHECK_BTB_TEMP = 38,
  USB_PPS_MOS_CTRL = 39,
  USB_PPS_CP_MODE_INIT = 40,
  USB_PPS_CHECK_AUTHENTICATE = 41,
  USB_PPS_GET_AUTHENTICATE = 42,
  USB_PPS_GET_CP_VBUS = 43,
  USB_PPS_GET_CP_MASTER_IBUS = 44,
  USB_PPS_GET_CP_SLAVE_IBUS = 45,
  USB_PPS_MOS_SLAVE_CTRL = 46,
  USB_PPS_GET_R_COOL_DOWN = 47,
  USB_PPS_GET_DISCONNECT_STATUS = 48,
  USB_PPS_VOOCPHY_ENABLE = 49,
  USB_IN_STATUS = 50,
  USB_GET_BATT_CURR = 51,
  USB_PPS_FORCE_SVOOC = 52,
  USB_SET_OVP_CFG = 53,
  USB_SET_UFCS_START = 54,
  USB_SET_UFCS_VOLT = 55,
  USB_SET_UFCS_CURRENT = 56,
  USB_GET_UFCS_STATUS = 57,
  USB_GET_DEV_INFO_L = 58,
  USB_GET_DEV_INFO_H = 59,
  USB_SET_WD_TIME = 60,
  USB_SET_EXIT = 61,
  USB_GET_SRC_INFO_L = 62,
  USB_GET_SRC_INFO_H = 63,
  USB_OTG_BOOST_CURRENT = 64,
  USB_SNS_STATUS = 65,
  USB_SET_UFCS_SM_PERIOD = 66,
  USB_SET_RERUN_AICL = 67,
  USB_SET_AICL_VOL = 68,
  USB_GET_AICL_VOL = 69,
  USB_SET_PLC_STATUS = 70,
};


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


  Status = glink_helper_init(ImageHandle, SystemTable);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "glink_helper_init failed\n", Status));
    goto error;
  }


  Status = gBS->LocateProtocol (&gGlinkHelperProtocolGuid, NULL, (VOID *)&mGlinkHelperProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate GlinkHelper Protocol! Status = %r\n", Status));
    goto error;
  }

  Status = gBS->LocateProtocol (&gQcomSPMIProtocolGuid, NULL, (VOID *)&mQcomSPMIProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate Qcom SPMI Protocol! Status = %r\n", Status));
    goto error;
  }

  EFI_PIL_PROTOCOL* mPILProtocol = 0;
  Status = gBS->LocateProtocol(&gEfiPilProtocolGuid, NULL, (VOID **)&mPILProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate PIL Protocol! Status = %r\n", Status));
    goto error;
  }
/*
  EFI_PMIC_GLINK_PROTOCOL* mPmicGlinkProtocol;
  Status = gBS->LocateProtocol (&gPmicGlinkProtocolGuid, NULL, (VOID *)&mPmicGlinkProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate Qcom PMIC glink Protocol! Status = %r\n", Status));
    goto error;
  }
*/
  DEBUG ((EFI_D_WARN, "PILProtocol->ProcessPilImage\n"));

  // Should we check the link status before loading the adsp firmware in case the charger library has already loeded it?
  Status = mPILProtocol->ProcessPilImage(L"FULL_ADSP");
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "ProcessPilImage Failed! Status = %r\n", Status));
  }

  DEBUG ((EFI_D_WARN, "mGlinkHelperProtocol->open\n"));
  glh_descriptor_t* glhd = mGlinkHelperProtocol->open("SMEM", "lpass", "PMIC_RTR_ADSP_APPS", 0);
  if(!glhd){
    DEBUG ((EFI_D_WARN, "mGlinkHelperProtocol->open failed\n"));
    goto error;
  }

  Status = mGlinkHelperProtocol->charger_send_sync(glhd, MSG_OP_CHARGER_USB_STATUS_SET, USB_OTG_AP_ENABLE, 1, 0, 0);
  DEBUG ((EFI_D_WARN, "USB_OTG_AP_ENABLE: %r\n", Status));
  Status = mGlinkHelperProtocol->charger_send_sync(glhd, MSG_OP_CHARGER_USB_STATUS_SET, USB_OEM_MISC_CTL, 0x51, 0, 0);
  DEBUG ((EFI_D_WARN, "USB_OEM_MISC_CTL: %r\n", Status));
  Status = mGlinkHelperProtocol->charger_send_sync(glhd, MSG_OP_CHARGER_USB_STATUS_SET, USB_TYPEC_SINKONLY, 0, 0, 0);
  DEBUG ((EFI_D_WARN, "USB_TYPEC_SINKONLY: %r\n", Status));
  Status = mGlinkHelperProtocol->charger_send_sync(glhd, MSG_OP_CHARGER_USB_STATUS_SET, USB_TYPEC_MODE, 0, 0, 0); // 0=DRP, 1=SNK, 2=SRC
  DEBUG ((EFI_D_WARN, "USB_TYPEC_MODE: %r\n", Status));
  Status = mGlinkHelperProtocol->charger_send_sync(glhd, MSG_OP_CHARGER_USB_STATUS_SET, USB_CCDETECT_HAPPENED, 1, 0, 0);
  DEBUG ((EFI_D_WARN, "USB_CCDETECT_HAPPENED: %r\n", Status));
  gBS->Stall(100*1000);
  Status = mGlinkHelperProtocol->charger_send_sync(glhd, MSG_OP_CHARGER_USB_STATUS_SET, USB_OTG_SWITCH, 1, 0, 0);
  DEBUG ((EFI_D_WARN, "USB_OTG_SWITCH: %r\n", Status));
  // Status = mGlinkHelperProtocol->charger_send_sync(glhd, MSG_OP_CHARGER_USB_STATUS_SET, USB_OTG_BOOST_CURRENT, 1000, 0, 0); // current limit in mA. Default value is 0, not sure what that means.
  // DEBUG ((EFI_D_WARN, "USB_OTG_BOOST_CURRENT: %r\n", Status));

  UINT32 response[6];
  Status = mGlinkHelperProtocol->charger_send_sync(glhd, MSG_OP_CHARGER_USB_STATUS_GET, USB_OTG_BOOST_CURRENT, 0, response, (UINTN[]){sizeof(response)});
  DEBUG ((EFI_D_WARN, "USB_OTG_BOOST_CURRENT: %d\n", response[4]));
  Status = mGlinkHelperProtocol->charger_send_sync(glhd, MSG_OP_CHARGER_USB_STATUS_GET, USB_MOISTURE_DET_EN, 0, response, (UINTN[]){sizeof(response)});
  DEBUG ((EFI_D_WARN, "USB_MOISTURE_DET_EN: %d\n", response[4]));
  Status = mGlinkHelperProtocol->charger_send_sync(glhd, MSG_OP_CHARGER_USB_STATUS_GET, USB_MOISTURE_DET_STS, 0, response, (UINTN[]){sizeof(response)});
  DEBUG ((EFI_D_WARN, "USB_MOISTURE_DET_STS: %d\n", response[4]));

  gBS->Stall(4*1000000);
  
  Status = mGlinkHelperProtocol->charger_send_sync(glhd, MSG_OP_CHARGER_USB_STATUS_SET, USB_OTG_VBUS_REGULATOR_ENABLE, 1, 0, 0);
  DEBUG ((EFI_D_WARN, "USB_OTG_VBUS_REGULATOR_ENABLE: %r\n", Status));

  mGlinkHelperProtocol->close(glhd);

  //gBS->Stall(4*1000000);
/*  DEBUG ((EFI_D_WARN, "mPmicGlinkProtocol->Connect\n"));

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
*/
  {
    static EFI_EVENT PollEvt;
    Status = gBS->CreateEvent(
      EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
      Poll, NULL, &PollEvt
    );
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "Failed to create TypeCHelper poll Event! Status = %r\n", Status));
      goto error;
    }

/*    Status = gBS->SetTimer(PollEvt, TimerPeriodic, 1000000);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "TypeCHelper SetTimer failed! Status = %r\n", Status));
      goto error;
    }*/
    gBS->SignalEvent(PollEvt);
  }

  mULogCtl->EnableLog(ULOGCTL_ANY_LOG, 0);
  DEBUG((EFI_D_WARN, "\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n\n"));
  gBS->Stall(5*1000000);
  return EFI_SUCCESS;

error:
  mULogCtl->EnableLog(ULOGCTL_ANY_LOG, 0);
  DEBUG((EFI_D_WARN, "\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n\n"));
  gBS->Stall(5*1000000);
  return Status;
}
