#ifndef ADSPUSBCDXE_H
#define ADSPUSBCDXE_H

#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/GlinkHelper.h>

#include "utils.h"

extern EFI_GUID gGlinkHelperProtocolGuid;
extern GLINK_HELPER_PROTOCOL* mGlinkHelperProtocol;
extern glh_descriptor_t* glhd;


// charger.c
EFI_STATUS charger_enable_notifications(void);
EFI_STATUS charger_battery_set_property(UINT32 battery_id, UINT32 property, UINT32 value);
EFI_STATUS charger_battery_get_property(UINT32 battery_id, UINT32 property, UINT32* ret_value);
EFI_STATUS charger_usb_set_property(UINT32 property, UINT32 value);
EFI_STATUS charger_usb_get_property(UINT32 property, UINT32* ret_value);
EFI_STATUS charger_wls_set_property(UINT32 property, UINT32 value);
EFI_STATUS charger_wls_get_property(UINT32 property, UINT32* ret_value);


// pan.c
#define OP_PAN_CMD_READ_REQ 0x14
#define OP_PAN_CMD_WRITE_REQ 0x15

#define OP_PAN_NOTIFY_IND 0x16

enum pan_altmode_cmd {
  PAN_ALTMODE_ENABLE = 0x10,
  PAN_ALTMODE_ACK,
  PAN_ALTMODE_READ_SEL,
};

EFI_STATUS pan_altmode_send_cmd(enum pan_altmode_cmd cmd, UINT32 arg);
EFI_STATUS pan_altmode_enable_notifications(void);
EFI_STATUS pan_altmode_ack(UINT8 port_index);


// ucsi.c
#define UCSI_NOTIFICATION 0x13

#define UCSI_ACK_CONNECTOR_CHANGE (1<<16)
#define UCSI_ACK_COMMAND_COMPLETE (1<<17)

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

enum {
  UCSI_PPM_RESET = 0x01,
  UCSI_CANCEL = 0x02,
  UCSI_CONNECTOR_RESET = 0x03,
  UCSI_ACK_CC_CI = 0x04,
  UCSI_SET_NOTIFICATION_ENABLE = 0x05,
  UCSI_GET_CAPABILITY = 0x06,
  UCSI_GET_CONNECTOR_CAPABILITY = 0x07,
  UCSI_SET_CCOM = 0x08,
  UCSI_SET_UOR = 0x09,
  // UCSI_SET_PDM = 0x0A, // obsolete and unimplemented
  UCSI_SET_PDR = 0x0B,
  UCSI_GET_ALTERNATE_MODES = 0x0C,
  UCSI_GET_CAM_SUPPORTED = 0x0D,
  UCSI_GET_CURRENT_CAM = 0x0E,
  UCSI_SET_NEW_CAM = 0x0F,
  UCSI_GET_PDOS = 0x10,
  UCSI_GET_CABLE_PROPERTY = 0x11,
  UCSI_GET_CONNECTOR_STATUS = 0x12,
  UCSI_GET_ERROR_STATUS = 0x13,
  UCSI_SET_POWER_LEVEL = 0x14,
  UCSI_GET_PD_MESSAGE = 0x15,
  UCSI_GET_ATTENTION_VDO = 0x16,
  // 0x17
  UCSI_GET_CAM_CS = 0x18,
  UCSI_LPM_FW_UPDATE_REQUEST = 0x19,
  UCSI_SECURITY_REQUEST = 0x1A,
  UCSI_SET_RETIMER_MODE = 0x1B,
  UCSI_SET_SINK_PATH = 0x1C,
};

struct ucsi_data {
  UINT16 version; // 8 major, 4 minor, 4 patch
  UINT16 reserved;
  UINT32 cci;
  UINT64 control;
  UINT8 message_in [0x10];
  UINT8 message_out[0x10];
};
_Static_assert(sizeof(struct ucsi_data) == 0x30, "UCSI data structure had unexpected size");

enum {
  UCSI_SN_COMMAND_COMPLETED, // (R)
  UCSI_SN_EXTERNAL_SUPPLY_CHANGE, // (O)
  UCSI_SN_POWER_OPERATION_MODE_CHANGE, // (R)
  UCSI_SN_ATTENTION, // (O)
  UCSI_SN_LPM_FW_UPDATE_REQUEST_FROM_PORT_PARTNER, // (O)
  UCSI_SN_SUPPORTED_PROVIDER_CAPABILITIES_CHANGE, // (O)
  UCSI_SN_NEGOTIATED_POWER_LEVEL_CHANGE, // (O)
  UCSI_SN_PD_RESET_COMPLETE, // (O)
  UCSI_SN_SUPPORTED_CAM_CHANGE, // (O)
  UCSI_SN_BATTERY_CHARGING_STATUS_CHANGE, // (R)
  UCSI_SN_SECURITY_REQUEST_FROM_PORT_PARTNER, // (O)
  UCSI_SN_CONNECTOR_PARTNER_CHANGE, // (R)
  UCSI_SN_POWER_DIRECTION_CHANGE, // (R)
  UCSI_SN_SET_RETIMER_MODE, // (O)
  UCSI_SN_CONNECT_CHANGE, // (R)
  UCSI_SN_ERROR, // (R)
  UCSI_SN_SINK_PATH_STATUS_CHANGE, //(R)
};

struct ucsi_notification {
  UINT32 cci; // cci
  UINT32 receiver;
  UINT32 reserved;
};

void ucsi_init(void);
void ucsi_onreceive(struct glh_descriptor* glhd, struct glink_hdr* data, UINTN size);

EFI_STATUS ucsi_write(const struct ucsi_data* ucsi_message);
EFI_STATUS ucsi_read(struct ucsi_data* ucsi_message);

#endif
