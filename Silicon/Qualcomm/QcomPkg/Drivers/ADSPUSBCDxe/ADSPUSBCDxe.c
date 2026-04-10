#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/GlinkHelper.h>


typedef struct _EFI_PIL_PROTOCOL {
  UINT64 Revision;
  EFI_STATUS (EFIAPI *ProcessPilImage) (IN CHAR16* Subsys);
} EFI_PIL_PROTOCOL;


extern EFI_GUID gEfiPilProtocolGuid;
static EFI_PIL_PROTOCOL* mPILProtocol;

extern EFI_GUID gGlinkHelperProtocolGuid;
static GLINK_HELPER_PROTOCOL* mGlinkHelperProtocol;


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
  EFI_STATUS Status = EFI_SUCCESS;

  Status = gBS->LocateProtocol(&gEfiPilProtocolGuid, NULL, (VOID **)&mPILProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate PIL Protocol! Status = %r\n", Status));
    goto error;
  }

  Status = gBS->LocateProtocol (&gGlinkHelperProtocolGuid, NULL, (VOID *)&mGlinkHelperProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate GlinkHelper Protocol! Status = %r\n", Status));
    goto error;
  }

  
  Status = mPILProtocol->ProcessPilImage(L"FULL_ADSP");
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "ProcessPilImage Failed! Status = %r\n", Status));
    goto error;
  }

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
  Status = mGlinkHelperProtocol->charger_send_sync(glhd, MSG_OP_CHARGER_USB_STATUS_SET, USB_OTG_SWITCH, 1, 0, 0);
  DEBUG ((EFI_D_WARN, "USB_OTG_SWITCH: %r\n", Status));
  Status = mGlinkHelperProtocol->charger_send_sync(glhd, MSG_OP_CHARGER_USB_STATUS_SET, USB_OTG_VBUS_REGULATOR_ENABLE, 1, 0, 0);
  DEBUG ((EFI_D_WARN, "USB_OTG_VBUS_REGULATOR_ENABLE: %r\n", Status));
  // Status = mGlinkHelperProtocol->charger_send_sync(glhd, MSG_OP_CHARGER_USB_STATUS_SET, USB_OTG_BOOST_CURRENT, 1000, 0, 0); // current limit in mA. Default value is 0, not sure what that means.
  // DEBUG ((EFI_D_WARN, "USB_OTG_BOOST_CURRENT: %r\n", Status));


  mGlinkHelperProtocol->close(glhd);


  return EFI_SUCCESS;
error:
  return Status;
}
