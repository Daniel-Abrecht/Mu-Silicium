#include "ADSPUSBCDxe.h"


struct charger_usb_property_request_msg {
  struct glink_hdr hdr;
  UINT32 battery_id;
  UINT32 property_id;
  UINT32 value;
};

struct charger_usb_property_response_msg {
  struct glink_hdr hdr;
  UINT32 property_id;
  UINT32 value;
  UINT32 ret_code;
};

EFI_STATUS charger_set_property(UINT32 opcode, UINT32 battery_id, UINT32 property, UINT32 value){
  if(!glhd) return EFI_NOT_READY;
  struct charger_usb_property_request_msg request = {
    .hdr = {
      .owner = MSG_OWNER_CHARGER,
      .type = MSG_TYPE_REQ_RESP,
      .opcode = opcode,
    },
    .battery_id = battery_id,
    .property_id = property,
    .value = value,
  };
  // Note: the response has a return code, but we currently don't check it.
  return mGlinkHelperProtocol->send_receive_sync(glhd, &request.hdr, sizeof(request), 0, 0);
}

EFI_STATUS charger_get_property(UINT32 opcode, UINT32 battery_id, UINT32 property, UINT32* ret_value){
  if(!glhd) return EFI_NOT_READY;
  struct charger_usb_property_request_msg request = {
    .hdr = {
      .owner = MSG_OWNER_CHARGER,
      .type = MSG_TYPE_REQ_RESP,
      .opcode = opcode,
    },
    .battery_id = battery_id,
    .property_id = property,
  };
  struct charger_usb_property_response_msg response = {0};
  UINTN response_size = sizeof(response);
  // Note: the response has a return code, but we currently don't check it.
  EFI_STATUS Status = mGlinkHelperProtocol->send_receive_sync(glhd, &request.hdr, sizeof(request), &response.hdr, &response_size);
  if(EFI_ERROR(Status))
    return Status;
  *ret_value = response.value;
  return EFI_SUCCESS;
}

// TODO: is there a way to disable notifications again?
// Also, they seam to be on by default anyway, does this even do anything?
EFI_STATUS charger_enable_notifications(void){
  if(!glhd) return EFI_NOT_READY;
  const struct set_notify_msg {
    struct glink_hdr hdr;
    UINT32 battery_id;
    UINT32 power_state;
    UINT32 low_capacity;
    UINT32 high_capacity;
  } msg = {
    {
      .owner = MSG_OWNER_CHARGER,
      // Note: the android driver uses MSG_TYPE_NOTIFY. The ADSP doesn't seam to care. The response has MSG_TYPE_REQ_RESP set.
      // We expect the type to match in request and response, and frankly MSG_TYPE_NOTIFY seams strange anyway, so we use
      // MSG_TYPE_REQ_RESP here.
      .type = MSG_TYPE_REQ_RESP,
      .opcode = MSG_OP_SET_NOTIFY_REQ,
    }
  };
  return mGlinkHelperProtocol->send_receive_sync(glhd, &msg.hdr, sizeof(msg), 0, 0);
}


EFI_STATUS charger_battery_set_property(UINT32 battery_id, UINT32 property, UINT32 value){
  return charger_set_property(MSG_OP_CHARGER_BATTERY_PROPERTY_SET, battery_id, property, value);
}

EFI_STATUS charger_battery_get_property(UINT32 battery_id, UINT32 property, UINT32* ret_value){
  return charger_get_property(MSG_OP_CHARGER_BATTERY_PROPERTY_GET, battery_id, property, ret_value);
}

EFI_STATUS charger_usb_set_property(UINT32 property, UINT32 value){
  return charger_set_property(MSG_OP_CHARGER_USB_PROPERTY_SET, 0, property, value);
}

EFI_STATUS charger_usb_get_property(UINT32 property, UINT32* ret_value){
  return charger_get_property(MSG_OP_CHARGER_USB_PROPERTY_GET, 0, property, ret_value);
}

EFI_STATUS charger_wls_set_property(UINT32 property, UINT32 value){
  return charger_set_property(MSG_OP_CHARGER_WLS_PROPERTY_SET, 0, property, value);
}

EFI_STATUS charger_wls_get_property(UINT32 property, UINT32* ret_value){
  return charger_get_property(MSG_OP_CHARGER_WLS_PROPERTY_GET, 0, property, ret_value);
}
