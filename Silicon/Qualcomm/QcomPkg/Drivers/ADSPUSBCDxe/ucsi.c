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


#define CMD_STATE_LIST \
  X(CMD_IDLE) \
  X(CMD_WRITE_IN_TRANSIT) \
  X(CMD_WRITE_SENT) \
  X(CMD_WRITE_DONE) \
  X(CMD_READ_IN_TRANSIT) \
  X(CMD_READ_DONE) \
  X(CMD_ACK_IN_TRANSIT) \
  X(CMD_ACK_DONE) \
  X(CMD_ERROR_HAPPENED) 

#define X(S) S,
enum cmd_state { CMD_STATE_LIST };
#undef X
#define X(S) #S,
static const char*const cmd_state_name[] = { CMD_STATE_LIST };
#undef X
#undef CMD_STATE_LIST

static UINT64 error_count;
static UINT64 ack_required;
static enum cmd_state state;

static bitset128_t connector_changed_set;

BOOLEAN work_pending;
static EFI_EVENT state_change_event;
static EFI_EVENT timeout_event;

STATIC VOID EFIAPI state_change_callback(IN EFI_EVENT Event, IN VOID *Context);
STATIC VOID EFIAPI timeout_callback(IN EFI_EVENT Event, IN VOID *Context);


struct ucsi_transaction {
  struct ucsi_data message;
  struct ucsi_transaction* next;
  BOOLEAN acknowledged;
  BOOLEAN done;
  BOOLEAN error;
};

static struct ucsi_transaction *transaction_fifo_start;
// transaction_fifo_end always points to transaction_fifo_start if transaction_fifo_start is null.
// Else, it points top the next field of the last ucsi_transaction entry in the list.
static struct ucsi_transaction **transaction_fifo_end=&transaction_fifo_start;

static void set_state(enum cmd_state new_state){
  DEBUG((EFI_D_WARN, "set_state %a -> %a\n", cmd_state_name[state], cmd_state_name[new_state]));
  error_count = 0;
  state = new_state;
  work_pending = TRUE;
  gBS->SignalEvent(state_change_event);
}

static EFI_STATUS ucsi_write_immediately(const struct ucsi_data* data){
  struct ucsi_msg msg = {.hdr={ MSG_OWNER_UCSI, MSG_TYPE_REQ_RESP, OP_UCSI_WRITE }};
  gBS->CopyMem(&msg.ucsi, (void*)data, sizeof(*data));
  return mGlinkHelperProtocol->send_sync(glhd, &msg.hdr, sizeof(msg));
}

static EFI_STATUS ucsi_send_command_immediately(UINT64 command){
  struct ucsi_msg msg = {.hdr={ MSG_OWNER_UCSI, MSG_TYPE_REQ_RESP, OP_UCSI_WRITE }};
  gBS->CopyMem(&msg.ucsi[OFFSET_OF(struct ucsi_data, control)], &command, sizeof(command));
  return mGlinkHelperProtocol->send_sync(glhd, &msg.hdr, sizeof(msg));
}

struct get_connector_status_in {
  UINT16 connector_status_change;                  //  0 -  15

  UINT16 power_operation_mode : 3;                 // 16 -  18
  UINT16 connect_status  : 1;                      // 19
  UINT16 power_direction : 1;                      // 20
  UINT16 connector_partner_flags : 8;              // 21 -  28
  UINT16 connector_partner_type : 3;               // 29 -  31

  UINT32 request_data_object;                      // 32 -  63

  UINT32 battery_charging_capability_status : 2;   // 64 -  65
  UINT32 provider_capabilities_limited_reason : 4; // 66 -  69
  UINT32 bcd_pd_version_operation_mode : 16;       // 70 -  85
  UINT32 orientation : 1;                          // 86
  UINT32 sink_path_status : 1;                     // 87
  UINT32 reverse_current_protection_status : 1;    // 88
  UINT32 reserved : 7;                             // 89 -  95

  UINT32 reserved_2;                               // 96 - 128
};
// We fill it in with bit shifts. At worst, it's going to be a bit less efficient.
//_Static_assert(sizeof(struct get_connector_status_in) == 0x10, "get_connector_status_in data structure had unexpected size");

// Ideally, the compiler should be able to turn this function into about 3 instructions.
struct get_connector_status_in parse_connector_status_record(const struct ucsi_data* message){
  const UINT64*restrict m = (UINT64*)message->message_in; // We assume little endian here
  const struct get_connector_status_in ret = {
    .connector_status_change = m[0],

    .power_operation_mode = m[0]>>16,
    .connect_status = m[0]>>19,
    .power_direction = m[0]>>20,
    .connector_partner_flags = m[0]>>21,
    .connector_partner_type = m[0]>>29,

    .request_data_object = m[0]>>32,

    .battery_charging_capability_status = m[1],
    .provider_capabilities_limited_reason = m[1]>>2,
    .bcd_pd_version_operation_mode = m[1]>>6,
    .orientation = m[1]>>22,
    .sink_path_status = m[1]>>23,
    .reverse_current_protection_status = m[1]>>24,
    .reserved = m[1]>>25,
    .reserved_2 = m[1]>>32,
  };
  return ret;
}

static void ontransactiondone(const struct ucsi_transaction* t, BOOLEAN error){
  if(error){
    DEBUG((EFI_D_WARN, "UCSI transaction error\n"));
    return;
  }
  if((t->message.control & 0xFF) == UCSI_GET_CONNECTOR_STATUS){
    ack_required |= UCSI_ACK_CONNECTOR_CHANGE;
    struct get_connector_status_in status = parse_connector_status_record(&t->message);
    int connector = (t->message.control>>16) & 0x7F;
    bitset128_unset(&connector_changed_set, connector);
    DEBUG((EFI_D_WARN, "\nUCSI_GET_CONNECTOR_STATUS: %d %d\n", connector, (int)status.connector_partner_type));
    hexdump(&t->message, 0x30);
  }
  DEBUG((EFI_D_WARN, "UCSI transaction done\n"));
}

static struct ucsi_transaction temp_transaction = { .done=TRUE };

// We need to do a lot of things in the right order just to handle a single UCSI command, 
// we need to defer sending stuff from the glink message callback because of reentrancy restrictions,
// and we may have to that while the ucsi_write / ucsi_read function is in a high tpl state where events can't be used.
// In all cases, we will just call this, and continue on with the next step.
static void ucsi_state_machine_tick(void){
  EFI_TPL OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
  static BOOLEAN in_state_machine = FALSE;
  static BOOLEAN transaction_in_progress = FALSE;
  if(in_state_machine || !work_pending){
    gBS->RestoreTPL(OldTpl);
    return;
  }
  work_pending = FALSE;
  in_state_machine = TRUE;
  const char* errormsg = 0;
  // enum cmd_state old_state = state;
  EFI_STATUS Status;
  next:;
  DEBUG((EFI_D_WARN, "ucsi_state_machine_tick: %a\n", cmd_state_name[state]));
  switch(state){
    case CMD_IDLE: {
      if(ack_required){
        state = CMD_ACK_IN_TRANSIT;
        UINT64 ack_flags = ack_required;
        ack_required = 0;
        gBS->RestoreTPL(OldTpl);
        Status = ucsi_send_command_immediately(UCSI_ACK_CC_CI | (ack_flags & 0x0000FFFFFFFFFFFF)); // This may call the ucsi_onreceive callback
        OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
        // If state no longer is IN_TRANSIT, we must've gotten a response already!
        if(EFI_ERROR(Status) && state == CMD_ACK_IN_TRANSIT){
          ack_required |= ack_flags;
          state = CMD_IDLE;
          errormsg = "ucsi_send_command_immediately UCSI_ACK_CC_CI failed";
          goto error;
        }
        error_count = 0;
        goto next;
      }
      if(transaction_fifo_start) start_transaction: {
        state = CMD_WRITE_IN_TRANSIT;
        gBS->RestoreTPL(OldTpl);
        Status = ucsi_write_immediately(&transaction_fifo_start->message); // This may call the ucsi_onreceive callback
        OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
        if(EFI_ERROR(Status) && state == CMD_WRITE_IN_TRANSIT){
          state = CMD_IDLE;
          errormsg = "ucsi_send_immediately UCSI_WRITE failed";
          goto error;
        }
        transaction_in_progress = TRUE;
        error_count = 0;
        goto next;
      }
      for(int i=0; i<sizeof(connector_changed_set.value) / sizeof(*connector_changed_set.value); i++){
        UINT64 mask = connector_changed_set.value[i];
        if(!mask) continue;
        int j;
        for(j=0; !(mask & (1<<j)); j++);
        if(mask & (1<<j)){
          temp_transaction = (struct ucsi_transaction){
            .message.control = UCSI_GET_CONNECTOR_STATUS | ((i*64+j)<<16),
            .next = transaction_fifo_start,
          };
          transaction_fifo_start = &temp_transaction;
          if(!transaction_fifo_end)
            transaction_fifo_end = &temp_transaction.next;
          goto start_transaction;
        }
      }
    }; break;
    case CMD_WRITE_IN_TRANSIT:
    case CMD_WRITE_SENT: {
      ack_required |= UCSI_ACK_COMMAND_COMPLETE;
    } break;
    case CMD_WRITE_DONE: { // This state is reached after an ACK is received
      ack_required |= UCSI_ACK_COMMAND_COMPLETE;
      state = CMD_READ_IN_TRANSIT;
      gBS->RestoreTPL(OldTpl);
      Status = mGlinkHelperProtocol->send_sync(glhd, &(struct glink_hdr){MSG_OWNER_UCSI, MSG_TYPE_REQ_RESP, OP_UCSI_READ}, sizeof(struct glink_hdr)); // This may call the ucsi_onreceive callback
      OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
      if(EFI_ERROR(Status) && state == CMD_READ_IN_TRANSIT){
        state = CMD_WRITE_DONE;
        errormsg = "ucsi_send_immediately UCSI_WRITE failed";
        goto error;
      }
      error_count = 0;
      transaction_fifo_start->acknowledged = TRUE;
    }; goto next;
    case CMD_READ_IN_TRANSIT: break;
    case CMD_READ_DONE: {
      struct ucsi_transaction* t = transaction_fifo_start;
      error_count = 0;
      state = CMD_IDLE;
      transaction_in_progress = FALSE;
      transaction_fifo_start = t->next;
      if(!transaction_fifo_start)
        transaction_fifo_end = &transaction_fifo_start;
      t->next = 0;
      gBS->RestoreTPL(OldTpl);
      ontransactiondone(t, FALSE);
      OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
      t->done = TRUE;
    } goto next;
    case CMD_ACK_IN_TRANSIT: break;
    case CMD_ACK_DONE: error_count=0; state=CMD_IDLE; goto next;
    case CMD_ERROR_HAPPENED: {
      error_count = 0;
      // TODO: Do something sensible to recover. Maybe a UCSI reset seqence or so.
      if(transaction_in_progress){
        transaction_in_progress = FALSE;
        struct ucsi_transaction* t = transaction_fifo_start;
        transaction_fifo_start = t->next;
        if(!transaction_fifo_start)
          transaction_fifo_end = &transaction_fifo_start;
        t->next = 0;
        t->error = TRUE;
        t->acknowledged = TRUE;
        t->done = TRUE;
        gBS->RestoreTPL(OldTpl);
        ontransactiondone(t, TRUE);
        OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
      }
      ack_required |= UCSI_ACK_COMMAND_COMPLETE | UCSI_ACK_CONNECTOR_CHANGE;
      state = CMD_IDLE;
      work_pending = TRUE;
      gBS->SignalEvent(state_change_event);
    } break;
    error: {
      DEBUG((EFI_D_ERROR, "ucsi_state_machine_tick: %a: %r\n", errormsg, Status));
      if(++error_count >= 3){
        state = CMD_ERROR_HAPPENED;
        goto next;
      }
    } break;
  }
  // DEBUG((EFI_D_WARN, "ucsi_state_machine_tick %a -> %a\n", cmd_state_name[old_state], cmd_state_name[state]));
  in_state_machine = FALSE;
  gBS->RestoreTPL(OldTpl);
  return;
}

STATIC VOID EFIAPI state_change_callback(IN EFI_EVENT Event, IN VOID *Context){
  ucsi_state_machine_tick();
}

STATIC VOID EFIAPI timeout_callback(IN EFI_EVENT Event, IN VOID *Context){
  set_state(CMD_ERROR_HAPPENED);
  work_pending = TRUE;
  ucsi_state_machine_tick();
}


// Used for ucsi_write / ucsi_read. We can't use temp_transaction for this, it may already be in use.
static struct ucsi_transaction sync_transaction = { .done=TRUE };

static EFI_STATUS poll(volatile BOOLEAN*const completion){
  if(*completion) return EFI_SUCCESS;
  if(work_pending){
    ucsi_state_machine_tick();
    if(*completion) return EFI_SUCCESS;
  }
  while(TRUE){
    // glink poll
    if(work_pending)
      ucsi_state_machine_tick();
    if(*completion) break;
    if(FALSE){
      timeout_callback(0,0);
      return EFI_TIMEOUT;
    }
    gBS->Stall(10*1000);
  }
  return EFI_SUCCESS;
}

EFI_STATUS ucsi_write(const struct ucsi_data* ucsi_message){
  EFI_TPL OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
  ASSERT(!sync_transaction.next); // This may occur if the function was interrupted, and then called again. Don't do that.
  gBS->CopyMem(&sync_transaction.message, (void*)ucsi_message, sizeof(*ucsi_message));
  sync_transaction.error = FALSE;
  sync_transaction.acknowledged = FALSE;
  sync_transaction.done = FALSE;
  sync_transaction.next = *transaction_fifo_end;
  *transaction_fifo_end = &sync_transaction;
  transaction_fifo_end = &sync_transaction.next;
  work_pending = TRUE;
  gBS->SignalEvent(state_change_event);
  gBS->RestoreTPL(OldTpl);
  EFI_STATUS Status = poll(&sync_transaction.acknowledged);
  if(EFI_ERROR(Status) || sync_transaction.error)
    return Status;
  return Status;
}

EFI_STATUS ucsi_read(struct ucsi_data* ucsi_message){
  EFI_STATUS Status = poll(&sync_transaction.done);
  if(EFI_ERROR(Status) || sync_transaction.error)
    return Status;
  gBS->CopyMem(ucsi_message, &sync_transaction.message, sizeof(*ucsi_message));
  return EFI_SUCCESS;
}

void ucsi_init(void){
  EFI_STATUS Status;
  Status = gBS->CreateEvent(
    EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
    state_change_callback, NULL, &state_change_event
  );
  if(EFI_ERROR(Status)){
    DEBUG ((EFI_D_ERROR, "ucsi_init: Failed to create state_change_event! Status = %r\n", Status));
    return;
  }
  Status = gBS->CreateEvent(
    EVT_TIMER, TPL_CALLBACK,
    timeout_callback, NULL, &timeout_event
  );
  if(EFI_ERROR(Status)){
    DEBUG ((EFI_D_ERROR, "ucsi_init: Failed to create state_change_event! Status = %r\n", Status));
    return;
  }
  // struct ucsi_data response;
  Status = ucsi_write(&(struct ucsi_data){ .control = UCSI_SET_NOTIFICATION_ENABLE | (0xFFFF<<16) });
  DEBUG ((EFI_D_WARN, "\nUCSI_SET_NOTIFICATION_ENABLE: %r\n", Status));
}

void ucsi_onreceive(struct glh_descriptor* glhd, struct glink_hdr* data, UINTN size){
  if(data->owner != MSG_OWNER_UCSI)
    return;
  size -= sizeof(struct glink_hdr);
  if(data->type == MSG_TYPE_REQ_RESP){
    if(data->opcode == OP_UCSI_READ){
      if(state == CMD_READ_IN_TRANSIT){
        if(size){
          // copying only version, reserved, cci, message_in
          // not copying control, message_out
          gBS->CopyMem(&transaction_fifo_start->message, (void*)(data+1), size > 8 ? 8 : size);
          if(size > 16)
            gBS->CopyMem(&transaction_fifo_start->message.message_in, (void*)(data+1)+16, size-16 > 16 ? 16 : size-16);
        }
        set_state(CMD_READ_DONE);
      }else{
        DEBUG((EFI_D_WARN, "Got UCSI_READ response, but not in state CMD_READ_IN_TRANSIT! Current state: %a\n", cmd_state_name[state]));
      }
    }else if(data->opcode == OP_UCSI_WRITE){
      if(state == CMD_WRITE_IN_TRANSIT){
        set_state(CMD_WRITE_SENT);
      }else if(state == CMD_ACK_IN_TRANSIT){
        set_state(CMD_ACK_DONE);
      }else{
        DEBUG((EFI_D_WARN, "Got UCSI_WRITE response, but not in state CMD_WRITE_IN_TRANSIT or CMD_ACK_IN_TRANSIT! Current state: %a\n", cmd_state_name[state]));
      }
    }
  }else if(data->type == MSG_TYPE_NOTIFY && data->opcode == UCSI_NOTIFICATION){
    struct ucsi_notification*restrict notification = (struct ucsi_notification*)(data+1);
    DEBUG((EFI_D_ERROR, "UCSI notification: %lX\n", notification->cci));
    int changed_connector = CCI_get_connector_change_indicator(notification->cci);
    if(changed_connector){
      // We do that after we've read a UCSI_GET_CONNECTOR_STATUS command. If we ack it early, we'll loose the status change bits.
      // Although, we won't actually use those anyway.
      // ack_required |= UCSI_ACK_CONNECTOR_CHANGE;
      bitset128_set(&connector_changed_set, changed_connector);
    }
    if(notification->cci & CCI_BIT_command_completed){
      ack_required |= UCSI_ACK_COMMAND_COMPLETE;
      if(state == CMD_WRITE_SENT){
        set_state(CMD_WRITE_DONE);
      }else{
        DEBUG((EFI_D_WARN, "Got UCSI_ACK_COMMAND_COMPLETE message, but not in state CMD_WRITE_SENT! Current state: %a\n", cmd_state_name[state]));
      }
    }
    work_pending = TRUE;
    gBS->SignalEvent(state_change_event);
  }
}

