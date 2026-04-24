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
#include "ucsi.h"

#endif
