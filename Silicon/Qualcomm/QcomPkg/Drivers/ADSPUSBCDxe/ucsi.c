#include "ADSPUSBCDxe.h"

#define WAIT_TIMEOUT 2000

#define OP_UCSI_READ      0x11
#define OP_UCSI_WRITE     0x12
#define UCSI_NOTIFICATION 0x13


struct ucsi_msg {
  struct glink_hdr hdr;
  UINT8 ucsi[0x30];
  UINT32 ret_code;
};

enum cmd_state {
  CMD_IDLE,
  CMD_COMMAND_PENDING,
  CMD_READ_REQUESTED,
  CMD_READ_PENDING,
};

struct response_state_t {
  BOOLEAN is_acknowledged;
  BOOLEAN is_read_done;
  BOOLEAN is_error;
  struct ucsi_msg response;
};

enum { RS_SYNC, RS_ASYNC };
struct response_state_t response_state[2] = {
  {
    .is_acknowledged = TRUE,
    .is_read_done = TRUE,
    .is_error = TRUE,
  },{
    .is_acknowledged = TRUE,
    .is_read_done = TRUE,
    .is_error = TRUE,
  }
};

// Note: Only one UCSI command can be processed at any time.
static BOOLEAN is_idle = TRUE;
static BOOLEAN is_async = TRUE;
static BOOLEAN connector_change_needs_ack = FALSE;
static enum cmd_state command_state = CMD_IDLE;
static EFI_EVENT command_state_change_event;

void command_state_change_callback(void);

STATIC VOID EFIAPI command_state_change_callback_wrapper(IN EFI_EVENT Event, IN VOID *Context){
  command_state_change_callback();
}

void ucsi_init(void){
  EFI_STATUS Status;
  Status = gBS->CreateEvent(
    EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
    command_state_change_callback_wrapper, NULL, &command_state_change_event
  );
  if(EFI_ERROR(Status)){
    DEBUG ((EFI_D_ERROR, "GlinkHelper: Failed to create timer event! Status = %r\n", Status));
    return;
  }
  struct ucsi_data response;
  Status = ucsi_write(&(struct ucsi_data){ .control = UCSI_SET_NOTIFICATION_ENABLE | (0xFFFF<<16) });
  DEBUG ((EFI_D_WARN, "\nUCSI_SET_NOTIFICATION_ENABLE: %r\n", Status));
  gBS->Stall(1000*1000);
  Status = ucsi_write(&(struct ucsi_data){ .control = UCSI_SET_NOTIFICATION_ENABLE | (0xFFFF<<16) });
  DEBUG ((EFI_D_WARN, "\nUCSI_SET_NOTIFICATION_ENABLE: %r\n", Status));
  gBS->Stall(1000*1000);
  Status = ucsi_write(&(struct ucsi_data){ .control = UCSI_SET_NOTIFICATION_ENABLE | (0xFFFF<<16) });
  DEBUG ((EFI_D_WARN, "\nUCSI_SET_NOTIFICATION_ENABLE: %r\n", Status));
  gBS->Stall(1000*1000);

  Status = ucsi_write(&(struct ucsi_data){ .control = UCSI_GET_CAPABILITY });
  DEBUG ((EFI_D_WARN, "\nUCSI_GET_CAPABILITY: %r\n", Status));
  Status = ucsi_read(&response);
  DEBUG ((EFI_D_WARN, "ucsi_read: %r\n", Status));
  gBS->Stall(1000*1000);
  Status = ucsi_write(&(struct ucsi_data){ .control = UCSI_GET_CAPABILITY });
  DEBUG ((EFI_D_WARN, "\nUCSI_GET_CAPABILITY: %r\n", Status));
  Status = ucsi_read(&response);
  DEBUG ((EFI_D_WARN, "ucsi_read: %r\n", Status));
}

static void ucsi_done_cb(BOOLEAN error){
  if(is_idle) return;
  struct response_state_t*restrict r = &response_state[is_async];
  if(error){
    r->is_error = TRUE;
    command_state = CMD_IDLE;
    r->is_acknowledged = TRUE;
    r->is_read_done = TRUE;
    is_idle = TRUE;
  }else{
    r->is_error = FALSE;
    command_state = CMD_IDLE;
    r->is_acknowledged = TRUE;
    r->is_read_done = TRUE;
    is_idle = TRUE;
  }
  struct ucsi_msg msg = {
    .hdr = {
      .owner = MSG_OWNER_UCSI,
      .type = MSG_TYPE_REQ_RESP,
      .opcode = OP_UCSI_WRITE,
    },
  };
  UINT64 ack = UCSI_ACK_CC_CI | UCSI_ACK_COMMAND_COMPLETE;
  if(connector_change_needs_ack)
    ack = UCSI_ACK_CONNECTOR_CHANGE;
  connector_change_needs_ack = FALSE;
  gBS->CopyMem(&msg.ucsi[OFFSET_OF(struct ucsi_data, control)], &ack, sizeof(ack));
  EFI_STATUS Status = mGlinkHelperProtocol->send_sync(glhd, &msg.hdr, sizeof(msg));
  DEBUG((EFI_D_WARN, "UCSI DONE: %d: %r\n", error, Status));
}

static EFI_STATUS ucsi_write_common(const struct ucsi_data* ucsi_message, BOOLEAN async, BOOLEAN skip_read){
  if(!glhd) return EFI_NOT_READY;
  struct ucsi_msg msg = {
    .hdr = {
      .owner = MSG_OWNER_UCSI,
      .type = MSG_TYPE_REQ_RESP,
      .opcode = OP_UCSI_WRITE,
    },
  };
  gBS->CopyMem(msg.ucsi, (void*)ucsi_message, sizeof(*ucsi_message));
  EFI_TPL OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
  struct response_state_t* r = &response_state[async];
  if(!async){
    // Since we already try writing the next sync command, we didn't call ucsi_read, we don't need to know the result.
    r->is_read_done = TRUE;
    if(command_state == CMD_READ_REQUESTED)
      ucsi_done_cb(FALSE);
  }
  EFI_STATUS Status = 0;
  while(!is_idle){
    gBS->RestoreTPL(OldTpl);
    Status = mGlinkHelperProtocol->poll(glhd, 0, &is_idle);
    if(EFI_ERROR(Status) && OldTpl == TPL_NOTIFY)
      command_state_change_callback();
    OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
  }
  is_idle = FALSE;
  is_async = async;
  command_state = CMD_COMMAND_PENDING;
  *r = (struct response_state_t){
    .is_read_done = skip_read,
  };
  gBS->RestoreTPL(OldTpl);
  Status = mGlinkHelperProtocol->send_sync(glhd, &msg.hdr, sizeof(msg));
  if(EFI_ERROR(Status)){
    command_state_change_callback();
    DEBUG((EFI_D_ERROR, "ucsi_write: send_sync failed: %r\n", Status));
    return Status;
  }
  if(!async){
    Status = mGlinkHelperProtocol->poll(glhd, 0, &r->is_acknowledged);
    if(EFI_ERROR(Status)){
      command_state_change_callback();
      DEBUG((EFI_D_ERROR, "ucsi_write: poll failed: %r\n", Status));
      return Status;
    }
  }else{
    gBS->SetTimer(command_state_change_event, TimerRelative, WAIT_TIMEOUT * 10);
  }
  return Status;
}

EFI_STATUS ucsi_write(const struct ucsi_data* ucsi_message){
  return ucsi_write_common(ucsi_message, FALSE, FALSE);
}

EFI_STATUS ucsi_read(struct ucsi_data* ucsi_message){
  if(!glhd) return EFI_NOT_READY;
  while(TRUE){
    EFI_TPL OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
    struct response_state_t* r = &response_state[RS_SYNC];
    if(r->is_error){
      gBS->RestoreTPL(OldTpl);
      return EFI_DEVICE_ERROR;
    }
    if(r->is_read_done){
      gBS->CopyMem(ucsi_message, &r->response, sizeof(*ucsi_message));
      gBS->RestoreTPL(OldTpl);
      return EFI_SUCCESS;
    }
    if(is_async){
      gBS->RestoreTPL(OldTpl);
      return EFI_DEVICE_ERROR;
    }
    gBS->RestoreTPL(OldTpl);
    EFI_STATUS Status = mGlinkHelperProtocol->poll(glhd, 0, &r->is_acknowledged);
    if(EFI_ERROR(Status) && OldTpl == TPL_NOTIFY)
      command_state_change_callback();
  }
  return EFI_DEVICE_ERROR;
}

void command_state_change_callback(void){
  struct response_state_t*restrict r = &response_state[is_async];
  DEBUG((EFI_D_WARN, "UCSI ccb; state: %d is_acknowledged: %d is_read_done: %d is_error: %d is_idle: %d\n",
         command_state, r->is_acknowledged, r->is_read_done, r->is_error, is_idle
  ));
  if(command_state == CMD_IDLE){
    ucsi_done_cb(FALSE);
    return;
  }
  if(command_state == CMD_READ_REQUESTED){
    if(r->is_read_done)
      return;
    command_state = CMD_READ_PENDING;
    struct glink_hdr msg = {
      .owner = MSG_OWNER_UCSI,
      .type = MSG_TYPE_REQ_RESP,
      .opcode = OP_UCSI_READ,
    };
    EFI_STATUS Status = mGlinkHelperProtocol->send_sync(glhd, &msg, sizeof(msg));
    if(EFI_ERROR(Status)){
      DEBUG((EFI_D_ERROR, "command_state_change_callback: send_sync failed: %r\n", Status));
      ucsi_done_cb(TRUE);
    }else{
      gBS->SetTimer(command_state_change_event, TimerRelative, WAIT_TIMEOUT * 10);
    }
  }else{
    ucsi_done_cb(TRUE);
  }
}

void ucsi_onreceive(struct glh_descriptor* glhd, struct glink_hdr* data, UINTN size){
  if(data->owner != MSG_OWNER_UCSI)
    return;
  struct response_state_t*restrict r = &response_state[is_async];
  if(data->type == MSG_TYPE_REQ_RESP){
    if(data->opcode == OP_UCSI_READ){
      if(size > sizeof(struct ucsi_msg))
        size = sizeof(struct ucsi_msg);
      if(!r->is_read_done){
        struct response_state_t*restrict r = &response_state[is_async];
        gBS->CopyMem(&r->response, data+1, size >= sizeof(r->response) ? sizeof(r->response) : size);
        command_state = CMD_IDLE;
        gBS->SignalEvent(command_state_change_event);
      }
      gBS->SetTimer(command_state_change_event, TimerCancel, 0);
    }
  }else if(data->type == MSG_TYPE_NOTIFY){
    if(data->opcode == UCSI_NOTIFICATION){
      struct ucsi_notification* notification = (struct ucsi_notification*)(data+1);
      if(notification->cci & CCI_BIT_error){
        DEBUG((EFI_D_ERROR, "UCSI: Error"));
        gBS->SignalEvent(command_state_change_event);
      }else{
        if(CCI_get_connector_change_indicator(notification->cci)){
          DEBUG((EFI_D_WARN, "UCSI: Connector change: connector %d\n", CCI_get_connector_change_indicator(notification->cci)));
          connector_change_needs_ack = TRUE;
          // if(is_idle)
          //   gBS->SignalEvent(command_state_change_event);
        }
        if(!r->is_acknowledged){
          gBS->SetTimer(command_state_change_event, TimerCancel, 0);
          if(notification->cci & (CCI_BIT_acknowledge_command|CCI_BIT_command_completed)){
            r->is_acknowledged = TRUE;
            r->is_error = FALSE;
            if(r->is_read_done){ // We set this early if we are not interested in the response
              command_state = CMD_IDLE;
              gBS->SignalEvent(command_state_change_event);
            }else{
              command_state = CMD_READ_REQUESTED;
              gBS->SignalEvent(command_state_change_event);
            }
          }
        }
      }
    }
  }
}

