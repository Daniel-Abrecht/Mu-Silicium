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

static BOOLEAN in_state_machine;

static void set_state(enum cmd_state new_state){
  DEBUG((EFI_D_WARN, "set_state %a -> %a\n", cmd_state_name[state], cmd_state_name[new_state]));
  error_count = 0;
  state = new_state;
  if(!in_state_machine)
    gBS->SignalEvent(state_change_event);
}

static void error_happened(void){
  if(++error_count < 3) return;
  set_state(CMD_ERROR_HAPPENED);
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

static void ontransactiondone(const struct ucsi_transaction* t){
  DEBUG((EFI_D_WARN, "UCSI transaction done\n"));
}

static struct ucsi_transaction temp_transaction;

// We need to do a lot of things in the right order just to handle a single UCSI command, 
// we need to defer sending stuff from the glink message callback because of reentrancy restrictions,
// and we may have to that while the ucsi_write / ucsi_read function is in a high tpl state where events can't be used.
// In all cases, we will just call this, and continue on with the next step.
static void ucsi_state_machine_tick(void){
  in_state_machine = TRUE;
  const char* errormsg = 0;
  // enum cmd_state old_state = state;
#define ERROR_CHECK(CONDITION, ...) \
  if(EFI_ERROR(Status)){ errormsg=(__VA_ARGS__); goto error; }
  EFI_STATUS Status;
  next: switch(state){
    case CMD_IDLE: {
      DEBUG((EFI_D_ERROR, "CMD_IDLE\n"));
      if(ack_required){
        Status = ucsi_send_command_immediately(UCSI_ACK_CC_CI | (ack_required & 0x0000FFFFFFFFFFFF));
        ERROR_CHECK(EFI_ERROR(Status), "ucsi_send_command_immediately UCSI_ACK_CC_CI failed");
        set_state(CMD_ACK_IN_TRANSIT);
        ack_required = 0;
        goto next;
      }
      if(transaction_fifo_start) start_transaction: {
        Status = ucsi_write_immediately(&transaction_fifo_start->message);
        ERROR_CHECK(EFI_ERROR(Status), "ucsi_send_immediately UCSI_WRITE failed");
        set_state(CMD_WRITE_IN_TRANSIT);
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
      Status = mGlinkHelperProtocol->send_sync(glhd, &(struct glink_hdr){MSG_OWNER_UCSI, MSG_TYPE_REQ_RESP, OP_UCSI_READ}, sizeof(struct glink_hdr));
      ERROR_CHECK(EFI_ERROR(Status), "send_sync UCSI_READ failed");
      set_state(CMD_READ_IN_TRANSIT);
      transaction_fifo_start->acknowledged = TRUE;
    }; break;
    case CMD_READ_IN_TRANSIT: break;
    case CMD_READ_DONE: {
      struct ucsi_transaction* t = transaction_fifo_start;
      set_state(CMD_IDLE);
      ontransactiondone(t);
      transaction_fifo_start = t->next;
      if(!transaction_fifo_start)
        transaction_fifo_end = &transaction_fifo_start;
      t->next = 0;
      t->done = TRUE;
    } goto next;
    case CMD_ACK_IN_TRANSIT: break;
    case CMD_ACK_DONE: set_state(CMD_IDLE); goto next;
    case CMD_ERROR_HAPPENED: {
      // TODO: Do something sensible to recover. Maybe a UCSI reset seqence or so.
      if(transaction_fifo_start){
        transaction_fifo_start->error = TRUE;
        transaction_fifo_start->acknowledged = TRUE;
        transaction_fifo_start->done = TRUE;
      }
      ack_required |= UCSI_ACK_COMMAND_COMPLETE | UCSI_ACK_CONNECTOR_CHANGE;
      set_state(CMD_IDLE);
    } break;
    error: {
      DEBUG((EFI_D_ERROR, "ucsi_state_machine_tick: %a: %r\n", errormsg, Status));
      error_happened(); // Note: error state is only entered after the 3rd failed attempt
      if(state == CMD_ERROR_HAPPENED)
        goto next;
    } break;
  }
  // DEBUG((EFI_D_WARN, "ucsi_state_machine_tick %a -> %a\n", cmd_state_name[old_state], cmd_state_name[state]));
  in_state_machine = FALSE;
  return;
}

STATIC VOID EFIAPI state_change_callback(IN EFI_EVENT Event, IN VOID *Context){
  ucsi_state_machine_tick();
}

STATIC VOID EFIAPI timeout_callback(IN EFI_EVENT Event, IN VOID *Context){
  set_state(CMD_ERROR_HAPPENED);
  ucsi_state_machine_tick();
}


// Used for ucsi_write / ucsi_read. We can't use temp_transaction for this, it may already be in use.
static struct ucsi_transaction sync_transaction;

static EFI_STATUS poll(volatile BOOLEAN*const completion){
  if(*completion) return EFI_SUCCESS;
  ucsi_state_machine_tick();
  if(*completion) return EFI_SUCCESS;
  while(TRUE){
    // glink poll
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
  if(data->type == MSG_TYPE_REQ_RESP){
    if(data->opcode == OP_UCSI_READ){
      if(state == CMD_READ_IN_TRANSIT){
        if(size > sizeof(struct ucsi_msg))
          size = sizeof(struct ucsi_msg);
        gBS->CopyMem(&transaction_fifo_start->message, (void*)data, size);
        set_state(CMD_READ_DONE);
      }else{
        DEBUG((EFI_D_WARN, "Got UCSI_READ response, but not in state CMD_READ_IN_TRANSIT!\n"));
      }
    }else if(data->opcode == OP_UCSI_WRITE){
      if(state == CMD_WRITE_IN_TRANSIT){
        if(size > sizeof(struct ucsi_msg))
          size = sizeof(struct ucsi_msg);
        gBS->CopyMem(&transaction_fifo_start->message, (void*)data, size);
        set_state(CMD_WRITE_SENT);
      }else if(state == CMD_ACK_IN_TRANSIT){
        set_state(CMD_ACK_DONE);
      }else{
        DEBUG((EFI_D_WARN, "Got UCSI_WRITE response, but not in state CMD_WRITE_IN_TRANSIT or CMD_ACK_IN_TRANSIT!\n"));
      }
    }
  }else if(data->type == MSG_TYPE_NOTIFY && data->opcode == UCSI_NOTIFICATION){
    struct ucsi_notification*restrict notification = (struct ucsi_notification*)(data+1);
    int changed_connector = CCI_get_connector_change_indicator(notification->cci);
    if(changed_connector){
      ack_required |= UCSI_ACK_CONNECTOR_CHANGE;
      bitset128_set(&connector_changed_set, changed_connector);
    }
    if(notification->cci & CCI_BIT_command_completed)
      ack_required |= UCSI_ACK_COMMAND_COMPLETE;
    gBS->SignalEvent(state_change_event);
  }
}

