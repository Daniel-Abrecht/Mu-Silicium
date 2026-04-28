#include "ucsi.h"
#include <Library/BitmapLib.h>
#include <Library/DeadlineLib.h>
#include <Protocol/GlinkHelper.h>


#define ACK_TIMEOUT_DURATION 500
#define TRANSACTION_TIMEOUT_DURATION 2000
#define READ_TIMEOUT_DURATION 500

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

enum init_state {
  INIT_START,
  INIT_STEP_SET_NOTIFICATION_START,
  INIT_STEP_SET_NOTIFICATION,
  INIT_STEP_GET_CAPABILITY_START,
  INIT_STEP_GET_CAPABILITY,
  INIT_DONE
};

static UINT64 error_count;
static UINT64 ack_required;
static enum cmd_state state;
static enum init_state init_state;
static BOOLEAN init_done;

static UINTN connector_changed_set[BITMAP_NUM_WORDS(0x80)];
static BOOLEAN error_notification_received;

BOOLEAN work_pending;
static EFI_EVENT state_change_event;

static EFI_EVENT timeout_event;
BOOLEAN timeout_active;
static Deadline timeout_deadline;

STATIC VOID EFIAPI state_change_callback(IN EFI_EVENT Event, IN VOID *Context);
STATIC VOID EFIAPI timeout_callback(IN EFI_EVENT Event, IN VOID *Context);

extern EFI_GUID gGlinkHelperProtocolGuid;
static GLINK_HELPER_PROTOCOL* mGlinkHelperProtocol;
static glh_descriptor_t* glhd;

struct ucsi_transaction {
  struct ucsi_data message;
  struct ucsi_transaction* next;
  BOOLEAN acknowledged;
  BOOLEAN done;
  BOOLEAN error;
};

static BOOLEAN transaction_in_progress; // Set if the first transaction transaction_fifo_start points to has been started already
static struct ucsi_transaction *transaction_fifo_start;
// transaction_fifo_end always points to transaction_fifo_start if transaction_fifo_start is null.
// Else, it points top the next field of the last ucsi_transaction entry in the list.
static struct ucsi_transaction **transaction_fifo_end=&transaction_fifo_start;


static void timeout_start(UINT32 duration){
  DEBUG((EFI_D_WARN, "timeout_start\n"));
  timeout_active = TRUE;
  Deadline_set(&timeout_deadline, duration);
  gBS->SetTimer(timeout_event, TimerRelative, (UINT64)duration*10000);
}

static void timeout_clear(void){
  DEBUG((EFI_D_WARN, "timeout_clear\n"));
  timeout_active = FALSE;
  gBS->SetTimer(timeout_event, TimerCancel, 0);
}

static void ucsi_state_machine_tick(void);
static EFI_STATUS poll(volatile BOOLEAN*const completion);

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

// Ideally, the compiler should be able to turn this function into 2 or 3 instructions: https://godbolt.org/z/ncrfzqnY7
struct get_connector_status_in parse_connector_status_record(const struct ucsi_data* message){
  UINT64 m[2] = {((UINT64*)message->message_in)[0], ((UINT64*)message->message_in)[1]};
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

static void print_connector_status_record(const struct get_connector_status_in*restrict cs){
  DEBUG((EFI_D_WARN, "UCSI Connector Status:\n"));
  DEBUG((EFI_D_WARN, "|  Status Change:             0x%04X\n", cs->connector_status_change));
  DEBUG((EFI_D_WARN, "|  Power Operation Mode:      %d\n", (int)cs->power_operation_mode));
  DEBUG((EFI_D_WARN, "|  Connect Status:            %d\n", (int)cs->connect_status));
  DEBUG((EFI_D_WARN, "|  Power Direction:           %d\n", (int)cs->power_direction));
  DEBUG((EFI_D_WARN, "|  Partner Flags:             0x%02X\n", (int)cs->connector_partner_flags));
  DEBUG((EFI_D_WARN, "|  Partner Type:              %d\n", (int)cs->connector_partner_type));
  DEBUG((EFI_D_WARN, "|  Request Data Object:       0x%08X\n", cs->request_data_object));
  DEBUG((EFI_D_WARN, "|  Battery Charging Status:   %d\n", (int)cs->battery_charging_capability_status));
  DEBUG((EFI_D_WARN, "|  PD Limited Reason:         %d\n", (int)cs->provider_capabilities_limited_reason));
  DEBUG((EFI_D_WARN, "|  BCD PD Version:            0x%04X\n", (int)cs->bcd_pd_version_operation_mode));
  DEBUG((EFI_D_WARN, "|  Orientation:               %d\n", (int)cs->orientation));
  DEBUG((EFI_D_WARN, "|  Sink Path Status:          %d\n", (int)cs->sink_path_status));
  DEBUG((EFI_D_WARN, "\\  Reverse Current Prot:      %d\n", (int)cs->reverse_current_protection_status));
}

static const char*const ucsi_error_status_flags_str[] = {
  "Unrecognized command",
  "Non-existent connector number",
  "Invalid command specific parameters",
  "Incompatible connector partner",
  "CC communication error",
  "Command unsuccessful due to dead battery condition",
  "Contract negotiation failure",
  "Overcurrent",
  "Undefined",
  "Port partner rejected swap",
  "Hard Reset",
  "PPM Policy Conflict",
  "Swap Rejected",
  "Reverse Current Protection",
  "Set Sink Path Rejected",
  "Reserved",
};

static void ontransactiondone(const struct ucsi_transaction* t, EFI_STATUS error, UINT16 ucsi_error_status){
  int connector = (t->message.control>>16) & 0x7F;
  if(ucsi_error_status){
    DEBUG((EFI_D_ERROR, "\nUCSI_GET_ERROR_STATUS: %d\n", connector));
    for(int i=0; i<16; i++){
      if(!(ucsi_error_status & (1<<i)))
        continue;
      const char* reason = ucsi_error_status_flags_str[i];
      DEBUG((EFI_D_ERROR, "| %a\n", reason));
    }
  }
  const int cmd = t->message.control & 0xFF;
  if(error){
    DEBUG((EFI_D_WARN, "UCSI transaction error\n"));
    return;
  }
  if(cmd == UCSI_GET_CONNECTOR_STATUS){
    ack_required |= UCSI_ACK_CONNECTOR_CHANGE;
    struct get_connector_status_in status = parse_connector_status_record(&t->message);
    BitmapClear(connector_changed_set, connector);
    DEBUG((EFI_D_WARN, "\nUCSI_GET_CONNECTOR_STATUS: %d\n", connector));
    print_connector_status_record(&status);
    //hexdump(&t->message, 0x30);
  }
  DEBUG((EFI_D_WARN, "UCSI transaction done\n"));
}

// Make sure to run this function in TPL_NOTIFY.

static void transaction_detach(struct ucsi_transaction* t, EFI_STATUS completed_status){
  EFI_TPL OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
  if(t->done){
    gBS->RestoreTPL(OldTpl);
    return;
  }
  if(t == transaction_fifo_start)
    transaction_in_progress = FALSE;
  for(struct ucsi_transaction** it = &transaction_fifo_start; *it; it=&(*it)->next){
    if(*it != t) continue;
    if(*transaction_fifo_end == t)
      transaction_fifo_end = it;
    *it = t->next;
    t->next = 0;
    t->error = FALSE;
    t->acknowledged = TRUE;
    t->done = TRUE;
    gBS->RestoreTPL(OldTpl);
    ontransactiondone(t, completed_status, EFI_ERROR(completed_status) ? UCSI_ESI_UNDEFINED : 0);
    return;
  }
  ASSERT(!"ucsi_transaction object in invalid state: wasn't marked as done, but not in queue either!");
}

// Adds a transaction to the end of the transaction queue
static EFI_STATUS transaction_enqueue(struct ucsi_transaction*restrict t, const struct ucsi_data*restrict ucsi_message){
  EFI_TPL OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
  if(!t->done){
    gBS->RestoreTPL(OldTpl);
    return EFI_ABORTED;
  }
  gBS->CopyMem(&t->message, (void*)ucsi_message, sizeof(*ucsi_message));
  t->error = FALSE;
  t->acknowledged = FALSE;
  t->done = FALSE;
  t->next = 0;
  *transaction_fifo_end = t;
  transaction_fifo_end = &t->next;
  work_pending = TRUE;
  gBS->SignalEvent(state_change_event);
  gBS->RestoreTPL(OldTpl);
  return EFI_SUCCESS;
}

// This transaction object is used internally for things like ACK_CC_CI, GET_CONNECTOR_STATUS, GET_ERROR_STATUS
// UCSI commands. They take a special role in the UCSI protocol, as they may need to be issued before other commands
// already in the queue, in the right order. So we make sure we always have a usable temp_transaction object for that.
static struct ucsi_transaction temp_transaction = {
  .acknowledged=TRUE,
  .done=TRUE,
};

// We need to do a lot of things in the right order just to handle a single UCSI command, 
// we need to defer sending stuff from the glink message callback because of reentrancy restrictions,
// and we may have to that while the ucsi_write / ucsi_read function is in a high tpl state where events can't be used.
// In all cases, we will just call this, and continue on with the next step if possible.
static void ucsi_state_machine_tick(void){
  EFI_TPL OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
  static BOOLEAN in_state_machine = FALSE;
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
      ASSERT(!transaction_in_progress || error_notification_received);

      if(ack_required){
        DEBUG((EFI_D_WARN, "ack_required\n"));
        state = CMD_ACK_IN_TRANSIT;
        UINT64 ack_flags = ack_required;
        ack_required = 0;
        timeout_start(ACK_TIMEOUT_DURATION);
        gBS->RestoreTPL(OldTpl);
        Status = ucsi_send_command_immediately(UCSI_ACK_CC_CI | (ack_flags & 0x0000FFFFFFFFFFFF)); // This may call the ucsi_onreceive callback
        OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
        // If state no longer is IN_TRANSIT, we must've gotten a response already!
        if(EFI_ERROR(Status) && state == CMD_ACK_IN_TRANSIT){
          ack_required |= ack_flags;
          timeout_clear();
          state = CMD_IDLE;
          errormsg = "ucsi_send_command_immediately UCSI_ACK_CC_CI failed";
          goto error;
        }
        error_count = 0;
        goto next;
      }

      if(error_notification_received){
        DEBUG((EFI_D_WARN, "error_notification_received\n"));
        error_count = 0xFF;
        temp_transaction = (struct ucsi_transaction){
          .message.control = UCSI_GET_ERROR_STATUS,
          .next = &temp_transaction == transaction_fifo_start ? 0 : transaction_fifo_start,
        };
        transaction_fifo_start = &temp_transaction;
        if(!transaction_fifo_end)
          transaction_fifo_end = &temp_transaction.next;
        error_notification_received = FALSE;
        goto start_transaction;
      }

      switch(init_state){
        case INIT_START: init_state = INIT_STEP_SET_NOTIFICATION;
        case INIT_STEP_SET_NOTIFICATION_START: {
          const UINT64 notifications = 0x1FFFF; // UCSI currently specifies 17 notifications
          temp_transaction = (struct ucsi_transaction){
            .message.control = UCSI_SET_NOTIFICATION_ENABLE | (notifications<<16),
            .next = transaction_fifo_start,
          };
          transaction_fifo_start = &temp_transaction;
          if(!transaction_fifo_end)
            transaction_fifo_end = &temp_transaction.next;
          init_state = INIT_STEP_SET_NOTIFICATION;
          goto start_transaction;
        } break;
        case INIT_STEP_SET_NOTIFICATION:
          if(!temp_transaction.done)
            break;
          init_state = INIT_STEP_GET_CAPABILITY_START;
        case INIT_STEP_GET_CAPABILITY_START:
          temp_transaction = (struct ucsi_transaction){
            .message.control = UCSI_GET_CAPABILITY,
            .next = transaction_fifo_start,
          };
          transaction_fifo_start = &temp_transaction;
          if(!transaction_fifo_end)
            transaction_fifo_end = &temp_transaction.next;
          init_state = INIT_STEP_GET_CAPABILITY;
          goto start_transaction;
        case INIT_STEP_GET_CAPABILITY:
          if(!temp_transaction.done)
            break;
          init_state = INIT_DONE;
          init_done = TRUE;
        case INIT_DONE: break;
      }

      if(transaction_fifo_start) start_transaction: {
        DEBUG((EFI_D_WARN, "start_transaction\n"));
        state = CMD_WRITE_IN_TRANSIT;
        timeout_start(TRANSACTION_TIMEOUT_DURATION);
        gBS->RestoreTPL(OldTpl);
        Status = ucsi_write_immediately(&transaction_fifo_start->message); // This may call the ucsi_onreceive callback
        OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
        if(EFI_ERROR(Status) && state == CMD_WRITE_IN_TRANSIT){
          state = CMD_IDLE;
          timeout_clear();
          errormsg = "ucsi_send_immediately UCSI_WRITE failed";
          goto error;
        }
        transaction_in_progress = TRUE;
        error_count = 0;
        goto next;
      }

      // Handling of connector changes
      for(int i=0; i<sizeof(connector_changed_set) / sizeof(*connector_changed_set); i++){
        UINT64 mask = connector_changed_set[i];
        if(!mask) continue;
        int j;
        for(j=0; !(mask & (1<<j)); j++);
        if(mask & (1<<j)){
          DEBUG((EFI_D_WARN, "UCSI_GET_CONNECTOR_STATUS\n"));
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
      timeout_clear();
      ack_required |= UCSI_ACK_COMMAND_COMPLETE;
      transaction_fifo_start->acknowledged = TRUE;
      if(!transaction_in_progress){
        state = CMD_IDLE;
        goto next;
      }
      if(error_notification_received && temp_transaction.message.control != UCSI_GET_ERROR_STATUS){
        if(transaction_in_progress)
          transaction_fifo_start->error = TRUE;
        state = CMD_IDLE;
        goto next;
      }
      state = CMD_READ_IN_TRANSIT;
      timeout_start(READ_TIMEOUT_DURATION);
      gBS->RestoreTPL(OldTpl);
      Status = mGlinkHelperProtocol->send_sync(glhd, &(struct glink_hdr){MSG_OWNER_UCSI, MSG_TYPE_REQ_RESP, OP_UCSI_READ}, sizeof(struct glink_hdr)); // This may call the ucsi_onreceive callback
      OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
      if(EFI_ERROR(Status) && state == CMD_READ_IN_TRANSIT){
        state = CMD_WRITE_DONE;
        timeout_clear();
        errormsg = "ucsi_send_immediately UCSI_WRITE failed";
        goto error;
      }
      // transaction_fifo_start->acknowledged = TRUE;
      error_count = 0;
    }; goto next;
    case CMD_READ_IN_TRANSIT: break;
    case CMD_READ_DONE: {
      timeout_clear();
      error_count = 0;
      state = CMD_IDLE;
      if(transaction_in_progress){
        struct ucsi_transaction* t = transaction_fifo_start;
        error_notification_received = !!(t->message.cci & CCI_BIT_error); // BOOLEAN is not a _Bool
        DEBUG((EFI_D_WARN, "CCI %08X %08X %08X\n", t->message.cci, t->message.control, temp_transaction.message.control));
        if(error_notification_received && temp_transaction.message.control != UCSI_GET_ERROR_STATUS){
          t->error = TRUE;
          state = CMD_IDLE;
          goto next;
        }
        UINT16 ucsi_error_status = t->error ? UCSI_ESI_UNDEFINED : 0;
        if(temp_transaction.message.control == UCSI_GET_ERROR_STATUS){
          if(temp_transaction.next && temp_transaction.next->error){
            DEBUG((EFI_D_WARN, "CMD_READ_DONE: UCSI_GET_ERROR_STATUS %d\n", temp_transaction.message.control));
            ucsi_error_status = *(UINT16*)temp_transaction.message.message_in;
            if(!ucsi_error_status) // Some PPMs are just broken.
              ucsi_error_status = UCSI_ESI_UNDEFINED;
            t = temp_transaction.next;
            t->message.version = temp_transaction.message.version;
            t->message.reserved = temp_transaction.message.reserved;
            t->message.cci = temp_transaction.message.cci;
            temp_transaction.next = 0;
            temp_transaction.done = TRUE;
          }
        }
        transaction_fifo_start = t->next;
        if(!transaction_fifo_start)
          transaction_fifo_end = &transaction_fifo_start;
        transaction_in_progress = FALSE;
        t->next = 0;
        t->done = TRUE;
        gBS->RestoreTPL(OldTpl);
        ontransactiondone(t, t->error ? EFI_DEVICE_ERROR : EFI_SUCCESS, ucsi_error_status);
        OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
      }
      temp_transaction.message.control = 0;
    } goto next;
    case CMD_ACK_IN_TRANSIT: break;
    case CMD_ACK_DONE:
      timeout_clear();
      error_count=0;
      state=CMD_IDLE;
      goto next;
    case CMD_ERROR_HAPPENED: {
      timeout_clear();
      error_count = 0;
      state = CMD_IDLE;
      if(error_notification_received && temp_transaction.message.control != UCSI_GET_ERROR_STATUS){
        if(transaction_in_progress)
          transaction_fifo_start->error = TRUE;
        goto next;
      }
      ack_required |= UCSI_ACK_COMMAND_COMPLETE | UCSI_ACK_CONNECTOR_CHANGE;
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
        ontransactiondone(t, EFI_DEVICE_ERROR, UCSI_ESI_UNDEFINED);
        OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
      }
      temp_transaction.message.control = 0;

      // RESET !!!
      {
        DEBUG((EFI_D_ERROR, "ucsi: Attempting to reset PPM!\n"));
        gBS->RestoreTPL(OldTpl);
        Status = ucsi_send_command_immediately(UCSI_PPM_RESET); // This may call the ucsi_onreceive callback
        if(EFI_ERROR(Status)){
          DEBUG((EFI_D_ERROR, "ucsi: Sending PPM reset command failed!\n"));
        }else{
          OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
          init_state = INIT_START;
          init_done = FALSE;
          ack_required = 0;
          state = CMD_IDLE; // TODO: wait a bit before continuing with the init sequence
        }
      }

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
  EFI_TPL OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
  DEBUG((EFI_D_WARN, "timeout_callback\n"));
  if(!timeout_active){
    gBS->RestoreTPL(OldTpl);
    return;
  }
  timeout_active = FALSE;
  set_state(CMD_ERROR_HAPPENED);
  work_pending = TRUE;
  gBS->RestoreTPL(OldTpl);
  ucsi_state_machine_tick();
}


static EFI_STATUS poll(volatile BOOLEAN*const completion){
  if(*completion) return EFI_SUCCESS;
  if(work_pending){
    ucsi_state_machine_tick();
    if(*completion) return EFI_SUCCESS;
  }
  while(TRUE){
    if(!timeout_active)
      return *completion ? EFI_SUCCESS : EFI_TIMEOUT;
    mGlinkHelperProtocol->poll(glhd);
    if(work_pending)
      ucsi_state_machine_tick();
    if(*completion) break;
    if(Deadline_has_expired(&timeout_deadline)){
      timeout_callback(0,0);
      return EFI_TIMEOUT;
    }
    gBS->Stall(10*1000);
  }
  return EFI_SUCCESS;
}

static EFI_STATUS ucsi_write(struct ucsi_transaction* t, const struct ucsi_data* ucsi_message){
  switch(ucsi_message->control & 0xFF){
    case UCSI_PPM_RESET:
    case UCSI_CANCEL:
    case UCSI_ACK_CC_CI:
      return EFI_SUCCESS;
  }
  transaction_detach(t, EFI_ABORTED);
  return transaction_enqueue(t, ucsi_message);
}

EFI_STATUS ucsi_write_sync(ucsi_transaction_sync_t* st, const struct ucsi_data* ucsi_message){
  struct ucsi_transaction* t = (struct ucsi_transaction*)st;
  EFI_STATUS Status;
  Status = ucsi_write(t, ucsi_message);
  if(EFI_ERROR(Status))
    return Status;
  Status = poll(&t->acknowledged);
  if(EFI_ERROR(Status))
    return Status;
  if(t->error)
    return EFI_DEVICE_ERROR;
  return Status;
}

EFI_STATUS ucsi_write_async(ucsi_transaction_async_t* st, const struct ucsi_data* ucsi_message){
  struct ucsi_transaction* t = (struct ucsi_transaction*)st;
  return ucsi_write((struct ucsi_transaction*)t, ucsi_message);
}

EFI_STATUS ucsi_read_sync(ucsi_transaction_sync_t* st, struct ucsi_data* ucsi_message){
  struct ucsi_transaction* t = (struct ucsi_transaction*)st;
  EFI_STATUS Status = poll(&t->done);
  if(EFI_ERROR(Status))
    return Status;
  if(t->error)
    return EFI_DEVICE_ERROR;
  gBS->CopyMem(ucsi_message, &t->message, sizeof(*ucsi_message));
  return EFI_SUCCESS;
}

static void onreceive(struct glh_descriptor* glhd, struct glink_hdr* data, UINTN size){
  if(data->owner != MSG_OWNER_UCSI)
    return;
  size -= sizeof(struct glink_hdr);
  if(data->type == MSG_TYPE_REQ_RESP){
    if(data->opcode == OP_UCSI_READ){
      if(state == CMD_READ_IN_TRANSIT){
        if(transaction_in_progress && size){
          // copying only version, reserved, cci, message_in
          // not copying control, message_out
          gBS->CopyMem(&transaction_fifo_start->message, (void*)(data+1), size > 8 ? 8 : size);
          if(size > 16)
            gBS->CopyMem(&transaction_fifo_start->message.message_in, (void*)(data+1)+16, size-16 > 16 ? 16 : size-16);
        }
        set_state(CMD_READ_DONE);
      }else{
        DEBUG((EFI_D_ERROR, "ucsi: got UCSI_READ response, but not in state CMD_READ_IN_TRANSIT! Current state: %a\n", cmd_state_name[state]));
      }
    }else if(data->opcode == OP_UCSI_WRITE){
      if(state == CMD_WRITE_IN_TRANSIT){
        set_state(CMD_WRITE_SENT);
      }else if(state == CMD_ACK_IN_TRANSIT){
        set_state(CMD_ACK_DONE);
      }else{
        DEBUG((EFI_D_ERROR, "ucsi: got UCSI_WRITE response, but not in state CMD_WRITE_IN_TRANSIT or CMD_ACK_IN_TRANSIT! Current state: %a\n", cmd_state_name[state]));
      }
    }
  }else if(data->type == MSG_TYPE_NOTIFY && data->opcode == UCSI_NOTIFICATION){
    struct ucsi_notification*restrict notification = (struct ucsi_notification*)(data+1);
    DEBUG((EFI_D_WARN, "ucsi: notification: %lX\n", notification->cci));
    int changed_connector = CCI_get_connector_change_indicator(notification->cci);
    if(changed_connector){
      // We do that after we've read a UCSI_GET_CONNECTOR_STATUS command. If we ack it early, we'll loose the status change bits.
      // Although, we won't actually use those anyway.
      // ack_required |= UCSI_ACK_CONNECTOR_CHANGE;
      BitmapSet(connector_changed_set, changed_connector);
    }
    if(notification->cci & CCI_BIT_command_completed){
      ack_required |= UCSI_ACK_COMMAND_COMPLETE;
      if(state == CMD_WRITE_SENT){
        set_state(CMD_WRITE_DONE);
      }else{
        DEBUG((EFI_D_WARN, "ucsi: got UCSI_ACK_COMMAND_COMPLETE message, but not in state CMD_WRITE_SENT! Current state: %a\n", cmd_state_name[state]));
      }
    }
    if(notification->cci & CCI_BIT_error){
      if(!error_notification_received)
        DEBUG((EFI_D_ERROR, "ucsi: receiver ERROR notification!\n"));
      error_notification_received = TRUE;
    }
    work_pending = TRUE;
    gBS->SignalEvent(state_change_event);
  }
}


VOID EFIAPI ExitBootServices(IN EFI_EVENT Event, IN VOID *Context) {
  if(glhd) mGlinkHelperProtocol->close(glhd);
}

EFI_STATUS EFIAPI Main(
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
){
  DEBUG ((EFI_D_WARN, "ucsi_init\n"));
  EFI_STATUS Status;
  Status = gBS->CreateEvent(
    EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
    state_change_callback, NULL, &state_change_event
  );
  if(EFI_ERROR(Status)){
    DEBUG ((EFI_D_ERROR, "ucsi_init: Failed to create state_change_event! Status = %r\n", Status));
    return EFI_DEVICE_ERROR;
  }
  Status = gBS->CreateEvent(
    EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
    timeout_callback, NULL, &timeout_event
  );
  if(EFI_ERROR(Status)){
    DEBUG ((EFI_D_ERROR, "ucsi_init: Failed to create state_change_event! Status = %r\n", Status));
    return EFI_DEVICE_ERROR;
  }

  Status = gBS->LocateProtocol (&gGlinkHelperProtocolGuid, NULL, (VOID *)&mGlinkHelperProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate GlinkHelper Protocol! Status = %r\n", Status));
    return EFI_DEVICE_ERROR;
  }

  {
    static EFI_EVENT ExitEvt;
    Status = gBS->CreateEvent(EVT_SIGNAL_EXIT_BOOT_SERVICES, TPL_NOTIFY, ExitBootServices, NULL, &ExitEvt);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "CreateEvent for EVT_SIGNAL_EXIT_BOOT_SERVICES failed! Status = %r\n", Status));
      return EFI_DEVICE_ERROR;
    }
  }
  
  glhd = mGlinkHelperProtocol->open("SMEM", "lpass", "PMIC_RTR_ADSP_APPS", &(const struct glh_open_params){
    .onreceive = onreceive,
  });
  if(!glhd){
    DEBUG ((EFI_D_WARN, "mGlinkHelperProtocol->open failed\n"));
    return EFI_DEVICE_ERROR;
  }

  work_pending = TRUE;
  poll(&init_done);
  if(!init_done){
    DEBUG ((EFI_D_ERROR, "ucsi_init: Failed! Status = %r\n", Status));
    return EFI_DEVICE_ERROR;
  }
  DEBUG ((EFI_D_WARN, "ucsi_init done!\n"));

  // gBS->Stall(1000000);
  // DEBUG ((EFI_D_WARN, "\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n"));
  // set_state(CMD_ERROR_HAPPENED); // This is an easy way to test the error / reset handling

  while(1){
    gBS->Stall(1000000);
  }
  
  return EFI_SUCCESS;
}
