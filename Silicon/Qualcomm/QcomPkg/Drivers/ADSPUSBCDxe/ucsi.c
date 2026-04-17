#include "ADSPUSBCDxe.h"


#define OP_UCSI_READ      0x11
#define OP_UCSI_WRITE     0x12
#define UCSI_NOTIFICATION 0x13


struct ucsi_msg {
  struct glink_hdr hdr;
  UINT8 ucsi[0x30];
  UINT32 ret_code;
};


EFI_STATUS ucsi_write(const struct ucsi_data* ucsi_message){
  if(!glhd) return EFI_NOT_READY;
  struct ucsi_msg msg = {
    .hdr = {
      .owner = MSG_OWNER_UCSI,
      .type = MSG_TYPE_REQ_RESP,
      .opcode = OP_UCSI_WRITE,
    },
  };
  gBS->CopyMem(msg.ucsi, (void*)ucsi_message, sizeof(*ucsi_message));
  return mGlinkHelperProtocol->send_receive_sync(glhd, &msg.hdr, sizeof(msg), 0, 0);
}

EFI_STATUS ucsi_read(struct ucsi_data* ucsi_message){
  if(!glhd) return EFI_NOT_READY;
  struct glink_hdr msg = {
    .owner = MSG_OWNER_UCSI,
    .type = MSG_TYPE_REQ_RESP,
    .opcode = OP_UCSI_READ,
  };
  struct ucsi_msg response_msg = {0};
  UINTN response_size = sizeof(response_msg);
  EFI_STATUS Status = mGlinkHelperProtocol->send_receive_sync(glhd, &msg, sizeof(msg), &response_msg.hdr, &response_size);
  if(response_size != sizeof(response_msg)){
    DEBUG((EFI_D_WARN, "Response to UCSI Write was smaller than expected!"));
  }
  gBS->CopyMem(ucsi_message, response_msg.ucsi, sizeof(*ucsi_message));
  return Status;
}

