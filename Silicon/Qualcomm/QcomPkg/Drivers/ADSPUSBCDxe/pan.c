#include "ADSPUSBCDxe.h"


EFI_STATUS pan_altmode_send_cmd(enum pan_altmode_cmd cmd, UINT32 arg){
  if(!glhd) return EFI_NOT_READY;
  struct {
    struct glink_hdr hdr;
    UINT32 cmd;
    UINT32 arg;
    UINT32 reserved;
  } msg = {
    .hdr = {
      .owner = MSG_OWNER_CPAN,
      .type = MSG_TYPE_REQ_RESP,
      .opcode = OP_PAN_CMD_WRITE_REQ,
    },
    .cmd = cmd,
    .arg = arg,
  };
  return mGlinkHelperProtocol->send_receive_sync(glhd, &msg.hdr, sizeof(msg), 0, 0);
}

EFI_STATUS pan_altmode_enable_notifications(void){
  if(!glhd) return EFI_NOT_READY;
  return pan_altmode_send_cmd(PAN_ALTMODE_ENABLE, 0);
}

EFI_STATUS pan_altmode_ack(UINT8 port_index){
  if(!glhd) return EFI_NOT_READY;
  return pan_altmode_send_cmd(PAN_ALTMODE_ACK, port_index);
}



////////
// The following data structures are for OP_CMD_READ_REQ
// They are not currently used, but maybe they'll be useful in the future.
////////

enum usbc_pan_data_type {
  PAN_DATA_PENDING,
  PAN_DATA_PORT_PIN_ASSIGNMENT,
  // PAN_DATA_HSUSB_PORT_DETECTION, // not implemented in ADSP
};

struct usbc_pan_data {
  enum usbc_pan_data_type type;
};

struct usbc_pan_data_port_pin_assignment_entry {
  unsigned char port_index;
  unsigned char orientation;
  unsigned char mux_ctrl;
  unsigned char dpam_hpd;
};

struct usbc_pan_data_port_pin_assignment {
  enum usbc_pan_data_type type; // PAN_DATA_PORT_PIN_ASSIGNMENT
  struct usbc_pan_data_port_pin_assignment_entry port[];
};

////////
