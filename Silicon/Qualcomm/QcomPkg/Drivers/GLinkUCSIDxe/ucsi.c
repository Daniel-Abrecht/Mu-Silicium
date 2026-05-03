#include "ucsi.h"
#include <Protocol/GlinkHelper.h>

// Everything is in ms
#define ACK_TIMEOUT_DURATION          500
#define TRANSACTION_TIMEOUT_DURATION 2000
#define READ_TIMEOUT_DURATION         500
#define RESET_WAIT_DURATION          1000

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
  X(CMD_ERROR_HAPPENED) \
  X(CMD_RESET_DELAY)

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


extern EFI_GUID gGlinkHelperProtocolGuid;
GLINK_HELPER_PROTOCOL* mGlinkHelperProtocol;

STATIC VOID EFIAPI state_change_callback(IN EFI_EVENT Event, IN VOID *Context);
STATIC VOID EFIAPI timeout_callback(IN EFI_EVENT Event, IN VOID *Context);

static void timeout_start(struct glink_ucsi* this, UINT32 duration){
  DEBUG((EFI_D_WARN, "timeout_start\n"));
  this->timeout_active = TRUE;
  Deadline_set(&this->timeout_deadline, duration);
  gBS->SetTimer(this->timeout_event, TimerRelative, (UINT64)duration*10000);
}

static void timeout_clear(struct glink_ucsi* this){
  DEBUG((EFI_D_WARN, "timeout_clear\n"));
  this->timeout_active = FALSE;
  gBS->SetTimer(this->timeout_event, TimerCancel, 0);
}

static void ucsi_state_machine_tick(struct glink_ucsi* this);
static EFI_STATUS poll(struct glink_ucsi* this, volatile BOOLEAN*const completion);

static void set_state(struct glink_ucsi* this, enum cmd_state new_state){
  DEBUG((EFI_D_WARN, "set_state %a -> %a\n", cmd_state_name[this->state], cmd_state_name[new_state]));
  this->error_count = 0;
  this->state = new_state;
  this->work_pending = TRUE;
  gBS->SignalEvent(this->state_change_event);
}

static EFI_STATUS ucsi_write_immediately(struct glink_ucsi* this, const struct ucsi_data* data){
  struct ucsi_msg msg = {.hdr={ MSG_OWNER_UCSI, MSG_TYPE_REQ_RESP, OP_UCSI_WRITE }};
  gBS->CopyMem(&msg.ucsi, (void*)data, sizeof(*data));
  return mGlinkHelperProtocol->send_sync(this->glink, &msg.hdr, sizeof(msg));
}

static EFI_STATUS ucsi_send_command_immediately(struct glink_ucsi* this, UINT64 command){
  struct ucsi_msg msg = {.hdr={ MSG_OWNER_UCSI, MSG_TYPE_REQ_RESP, OP_UCSI_WRITE }};
  gBS->CopyMem(&msg.ucsi[OFFSET_OF(struct ucsi_data, control)], &command, sizeof(command));
  return mGlinkHelperProtocol->send_sync(this->glink, &msg.hdr, sizeof(msg));
}

struct get_capability_in parse_capability_record(const struct ucsi_data* message){
  UINT64 m[2] = {((UINT64*)message->message_in)[0], ((UINT64*)message->message_in)[1]};
  const struct get_capability_in ret = {
    .bmAttributes = m[0],

    .bNumConnectors = m[0]>>32,
    .Reserved1 = m[0]>>39,
    .bmOptionalFeatures = m[0]>>40,

    .bNumAltModes = m[1],
    .Reserved2 = m[1]>>8,

    .bcdBCVersion = m[1]>>16,
    .bcdPDVersion = m[1]>>32,
    .bcdUSBTypeCVersion = m[1]>>48,
  };
  return ret;
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
  struct glink_ucsi* this = t->glink_ucsi;
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
    this->ack_required |= UCSI_ACK_CONNECTOR_CHANGE;
    BitmapClear(this->connector_changed_set, connector);
    struct get_connector_status_in status = parse_connector_status_record(&t->message);
    DEBUG((EFI_D_WARN, "\nUCSI_GET_CONNECTOR_STATUS: %d\n", connector));
    print_connector_status_record(&status);
    //hexdump(&t->message, 0x30);
  }
  DEBUG((EFI_D_WARN, "UCSI transaction done\n"));
}

// Make sure to run this function in TPL_NOTIFY.

static void transaction_detach(struct ucsi_transaction* t, EFI_STATUS completed_status){
  struct glink_ucsi* this = t->glink_ucsi;
  EFI_TPL OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
  if(t->done){
    gBS->RestoreTPL(OldTpl);
    return;
  }
  if(t == this->transaction_fifo_start)
    this->transaction_in_progress = FALSE;
  for(struct ucsi_transaction** it = &this->transaction_fifo_start; *it; it=&(*it)->next){
    if(*it != t) continue;
    if(*this->transaction_fifo_end == t)
      this->transaction_fifo_end = it;
    *it = t->next;
    t->next = 0;
    t->error = FALSE;
    t->acknowledged = TRUE;
    t->done = TRUE;
    gBS->RestoreTPL(OldTpl);
    ontransactiondone(t, completed_status, EFI_ERROR(completed_status) ? UCSI_ESI_UNDEFINED : 0);
    return;
  }
  ASSERT(!"ucsi_transaction object in invalid this->state: wasn't marked as done, but not in queue either!");
}

// Adds a transaction to the end of the transaction queue
static EFI_STATUS transaction_enqueue(struct ucsi_transaction*restrict t, const struct ucsi_data*restrict ucsi_message){
  struct glink_ucsi* this = t->glink_ucsi;
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
  *this->transaction_fifo_end = t;
  this->transaction_fifo_end = &t->next;
  this->work_pending = TRUE;
  gBS->SignalEvent(this->state_change_event);
  gBS->RestoreTPL(OldTpl);
  return EFI_SUCCESS;
}

// We need to do a lot of things in the right order just to handle a single UCSI command, 
// we need to defer sending stuff from the glink message callback because of reentrancy restrictions,
// and we may have to that while the ucsi_write / ucsi_read function is in a high tpl this->state where events can't be used.
// In all cases, we will just call this, and continue on with the next step if possible.
static void ucsi_state_machine_tick(struct glink_ucsi* this){
  EFI_TPL OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
  if(this->in_state_machine || !this->work_pending){
    gBS->RestoreTPL(OldTpl);
    return;
  }
  this->work_pending = FALSE;
  this->in_state_machine = TRUE;
  const char* errormsg = 0;
  // enum cmd_state old_state = this->state;
  EFI_STATUS Status;
next:;
  DEBUG((EFI_D_WARN, "ucsi_state_machine_tick: %a\n", cmd_state_name[this->state]));
  switch(this->state){
    case CMD_IDLE: {
      ASSERT(!this->transaction_in_progress || this->error_notification_received);

      if(this->ack_required){
        DEBUG((EFI_D_WARN, "this->ack_required\n"));
        this->state = CMD_ACK_IN_TRANSIT;
        UINT64 ack_flags = this->ack_required;
        this->ack_required = 0;
        timeout_start(this, ACK_TIMEOUT_DURATION);
        gBS->RestoreTPL(OldTpl);
        Status = ucsi_send_command_immediately(this, UCSI_ACK_CC_CI | (ack_flags & 0x0000FFFFFFFFFFFF)); // This may call the ucsi_onreceive callback
        OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
        // If this->state no longer is IN_TRANSIT, we must've gotten a response already!
        if(EFI_ERROR(Status) && this->state == CMD_ACK_IN_TRANSIT){
          this->ack_required |= ack_flags;
          timeout_clear(this);
          this->state = CMD_IDLE;
          errormsg = "ucsi_send_command_immediately UCSI_ACK_CC_CI failed";
          goto error;
        }
        this->error_count = 0;
        goto next;
      }

      if(this->error_notification_received){
        DEBUG((EFI_D_WARN, "this->error_notification_received\n"));
        this->error_count = 0xFF;
        this->temp_transaction = (struct ucsi_transaction){
          .glink_ucsi = this,
          .message.control = UCSI_GET_ERROR_STATUS,
          .next = &this->temp_transaction == this->transaction_fifo_start ? 0 : this->transaction_fifo_start,
        };
        this->transaction_fifo_start = &this->temp_transaction;
        if(!this->transaction_fifo_end)
          this->transaction_fifo_end = &this->temp_transaction.next;
        this->error_notification_received = FALSE;
        goto start_transaction;
      }

      switch(this->init_state){
        case INIT_START: this->init_state = INIT_STEP_SET_NOTIFICATION;
        case INIT_STEP_SET_NOTIFICATION_START: {
          const UINT64 notifications = 0x1FFFF; // UCSI currently specifies 17 notifications
          this->temp_transaction = (struct ucsi_transaction){
            .glink_ucsi = this,
            .message.control = UCSI_SET_NOTIFICATION_ENABLE | (notifications<<16),
            .next = this->transaction_fifo_start,
          };
          this->transaction_fifo_start = &this->temp_transaction;
          if(!this->transaction_fifo_end)
            this->transaction_fifo_end = &this->temp_transaction.next;
          this->init_state = INIT_STEP_SET_NOTIFICATION;
          goto start_transaction;
        } break;
        case INIT_STEP_SET_NOTIFICATION:
          if(!this->temp_transaction.done)
            break;
          this->init_state = INIT_STEP_GET_CAPABILITY_START;
        case INIT_STEP_GET_CAPABILITY_START:
          this->temp_transaction = (struct ucsi_transaction){
            .glink_ucsi = this,
            .message.control = UCSI_GET_CAPABILITY,
            .next = this->transaction_fifo_start,
          };
          this->transaction_fifo_start = &this->temp_transaction;
          if(!this->transaction_fifo_end)
            this->transaction_fifo_end = &this->temp_transaction.next;
          this->init_state = INIT_STEP_GET_CAPABILITY;
          goto start_transaction;
        case INIT_STEP_GET_CAPABILITY:
          if(!this->temp_transaction.done)
            break;
          this->capability = parse_capability_record(&this->temp_transaction.message);
          this->init_state = INIT_DONE;
          this->init_done = TRUE;
          connectors_init(this);
        case INIT_DONE: break;
      }

      if(this->transaction_fifo_start) start_transaction: {
        DEBUG((EFI_D_WARN, "start_transaction\n"));
        this->state = CMD_WRITE_IN_TRANSIT;
        timeout_start(this, TRANSACTION_TIMEOUT_DURATION);
        gBS->RestoreTPL(OldTpl);
        Status = ucsi_write_immediately(this, &this->transaction_fifo_start->message); // This may call the ucsi_onreceive callback
        OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
        if(EFI_ERROR(Status) && this->state == CMD_WRITE_IN_TRANSIT){
          this->state = CMD_IDLE;
          timeout_clear(this);
          errormsg = "ucsi_send_immediately UCSI_WRITE failed";
          goto error;
        }
        this->transaction_in_progress = TRUE;
        this->error_count = 0;
        goto next;
      }

      // Handling of connector changes
      for(int i=0; i<sizeof(this->connector_changed_set) / sizeof(*this->connector_changed_set); i++){
        UINT64 mask = this->connector_changed_set[i];
        if(!mask) continue;
        int j;
        for(j=0; !(mask & (1<<j)); j++);
        if(mask & (1<<j)){
          DEBUG((EFI_D_WARN, "UCSI_GET_CONNECTOR_STATUS\n"));
          this->temp_transaction = (struct ucsi_transaction){
            .glink_ucsi = this,
            .message.control = UCSI_GET_CONNECTOR_STATUS | ((i*64+j)<<16),
            .next = this->transaction_fifo_start,
          };
          this->transaction_fifo_start = &this->temp_transaction;
          if(!this->transaction_fifo_end)
            this->transaction_fifo_end = &this->temp_transaction.next;
          goto start_transaction;
        }
      }

    }; break;
    case CMD_WRITE_IN_TRANSIT:
    case CMD_WRITE_SENT: {
      this->ack_required |= UCSI_ACK_COMMAND_COMPLETE;
    } break;
    case CMD_WRITE_DONE: { // This this->state is reached after an ACK is received
      timeout_clear(this);
      this->ack_required |= UCSI_ACK_COMMAND_COMPLETE;
      this->transaction_fifo_start->acknowledged = TRUE;
      if(!this->transaction_in_progress){
        this->state = CMD_IDLE;
        goto next;
      }
      if(this->error_notification_received && this->temp_transaction.message.control != UCSI_GET_ERROR_STATUS){
        if(this->transaction_in_progress)
          this->transaction_fifo_start->error = TRUE;
        this->state = CMD_IDLE;
        goto next;
      }
      this->state = CMD_READ_IN_TRANSIT;
      timeout_start(this, READ_TIMEOUT_DURATION);
      gBS->RestoreTPL(OldTpl);
      Status = mGlinkHelperProtocol->send_sync(this->glink, &(struct glink_hdr){MSG_OWNER_UCSI, MSG_TYPE_REQ_RESP, OP_UCSI_READ}, sizeof(struct glink_hdr)); // This may call the ucsi_onreceive callback
      OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
      if(EFI_ERROR(Status) && this->state == CMD_READ_IN_TRANSIT){
        this->state = CMD_WRITE_DONE;
        timeout_clear(this);
        errormsg = "ucsi_send_immediately UCSI_WRITE failed";
        goto error;
      }
      // this->transaction_fifo_start->acknowledged = TRUE;
      this->error_count = 0;
    }; goto next;
    case CMD_READ_IN_TRANSIT: break;
    case CMD_READ_DONE: {
      timeout_clear(this);
      this->error_count = 0;
      this->state = CMD_IDLE;
      if(this->transaction_in_progress){
        struct ucsi_transaction* t = this->transaction_fifo_start;
        this->error_notification_received = !!(t->message.cci & CCI_BIT_error); // BOOLEAN is not a _Bool
        DEBUG((EFI_D_WARN, "CCI %08X %08X %08X\n", t->message.cci, t->message.control, this->temp_transaction.message.control));
        if(this->error_notification_received && this->temp_transaction.message.control != UCSI_GET_ERROR_STATUS){
          t->error = TRUE;
          this->state = CMD_IDLE;
          goto next;
        }
        UINT16 ucsi_error_status = t->error ? UCSI_ESI_UNDEFINED : 0;
        if(this->temp_transaction.message.control == UCSI_GET_ERROR_STATUS){
          if(this->temp_transaction.next && this->temp_transaction.next->error){
            DEBUG((EFI_D_WARN, "CMD_READ_DONE: UCSI_GET_ERROR_STATUS %d\n", this->temp_transaction.message.control));
            ucsi_error_status = *(UINT16*)this->temp_transaction.message.message_in;
            if(!ucsi_error_status) // Some PPMs are just broken.
              ucsi_error_status = UCSI_ESI_UNDEFINED;
            t = this->temp_transaction.next;
            t->message.version = this->temp_transaction.message.version;
            t->message.reserved = this->temp_transaction.message.reserved;
            t->message.cci = this->temp_transaction.message.cci;
            this->temp_transaction.next = 0;
            this->temp_transaction.done = TRUE;
          }
        }
        this->transaction_fifo_start = t->next;
        if(!this->transaction_fifo_start)
          this->transaction_fifo_end = &this->transaction_fifo_start;
        this->transaction_in_progress = FALSE;
        t->next = 0;
        t->done = TRUE;
        gBS->RestoreTPL(OldTpl);
        ontransactiondone(t, t->error ? EFI_DEVICE_ERROR : EFI_SUCCESS, ucsi_error_status);
        OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
      }
      this->temp_transaction.message.control = 0;
    } goto next;
    case CMD_ACK_IN_TRANSIT: break;
    case CMD_ACK_DONE:
      timeout_clear(this);
      this->error_count=0;
      this->state=CMD_IDLE;
      goto next;
    case CMD_ERROR_HAPPENED: {
      timeout_clear(this);
      this->error_count = 0;
      this->state = CMD_IDLE;
      if(this->error_notification_received && this->temp_transaction.message.control != UCSI_GET_ERROR_STATUS){
        if(this->transaction_in_progress)
          this->transaction_fifo_start->error = TRUE;
        goto next;
      }
      this->ack_required |= UCSI_ACK_COMMAND_COMPLETE | UCSI_ACK_CONNECTOR_CHANGE;
      if(this->transaction_in_progress){
        this->transaction_in_progress = FALSE;
        struct ucsi_transaction* t = this->transaction_fifo_start;
        this->transaction_fifo_start = t->next;
        if(!this->transaction_fifo_start)
          this->transaction_fifo_end = &this->transaction_fifo_start;
        t->next = 0;
        t->error = TRUE;
        t->acknowledged = TRUE;
        t->done = TRUE;
        gBS->RestoreTPL(OldTpl);
        ontransactiondone(t, EFI_DEVICE_ERROR, UCSI_ESI_UNDEFINED);
        OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
      }
      this->temp_transaction.message.control = 0;

      // RESET !!!
      {
        DEBUG((EFI_D_ERROR, "ucsi: Attempting to reset PPM!\n"));
        gBS->RestoreTPL(OldTpl);
        Status = ucsi_send_command_immediately(this, UCSI_PPM_RESET); // This may call the ucsi_onreceive callback
        OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
        if(EFI_ERROR(Status)){
          DEBUG((EFI_D_ERROR, "ucsi: Sending PPM reset command failed!\n"));
        }else{
          connectors_destroy(this);
          this->init_state = INIT_START;
          this->init_done = FALSE;
          this->ack_required = 0;
          this->state = CMD_RESET_DELAY; // TODO: wait a bit before continuing with the init sequence
          timeout_start(this, RESET_WAIT_DURATION);
        }
      }

      this->work_pending = TRUE;
      gBS->SignalEvent(this->state_change_event);
    } break;
    case CMD_RESET_DELAY: {
      if(Deadline_has_expired(&this->timeout_deadline)){
        timeout_clear(this);
        this->state = CMD_IDLE;
      }
      this->work_pending = TRUE;
    } break;
    error: {
      DEBUG((EFI_D_ERROR, "ucsi_state_machine_tick: %a: %r\n", errormsg, Status));
      if(++this->error_count >= 3){
        this->state = CMD_ERROR_HAPPENED;
        goto next;
      }
    } break;
  }
  // DEBUG((EFI_D_WARN, "ucsi_state_machine_tick %a -> %a\n", cmd_state_name[old_state], cmd_state_name[this->state]));
  this->in_state_machine = FALSE;
  gBS->RestoreTPL(OldTpl);
  return;
}

STATIC VOID EFIAPI state_change_callback(IN EFI_EVENT Event, IN VOID *Context){
  struct glink_ucsi* this = Context;
  ucsi_state_machine_tick(this);
}

STATIC VOID EFIAPI timeout_callback(IN EFI_EVENT Event, IN VOID *Context){
  struct glink_ucsi* this = Context;
  EFI_TPL OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
  DEBUG((EFI_D_WARN, "timeout_callback\n"));
  if(!this->timeout_active){
    gBS->RestoreTPL(OldTpl);
    return;
  }
  this->timeout_active = FALSE;
  if(this->state == CMD_RESET_DELAY){
    set_state(this, CMD_IDLE);
  }else{
    set_state(this, CMD_ERROR_HAPPENED);
  }
  this->work_pending = TRUE;
  gBS->RestoreTPL(OldTpl);
  ucsi_state_machine_tick(this);
}


static EFI_STATUS poll(struct glink_ucsi* this, volatile BOOLEAN*const completion){
  if(*completion) return EFI_SUCCESS;
  if(this->work_pending){
    ucsi_state_machine_tick(this);
    if(*completion) return EFI_SUCCESS;
  }
  while(TRUE){
    if(!this->timeout_active)
      return *completion ? EFI_SUCCESS : EFI_TIMEOUT;
    mGlinkHelperProtocol->poll(this->glink);
    if(this->work_pending)
      ucsi_state_machine_tick(this);
    if(*completion) break;
    if(Deadline_has_expired(&this->timeout_deadline)){
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
  Status = poll(t->glink_ucsi, &t->acknowledged);
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
  EFI_STATUS Status = poll(t->glink_ucsi, &t->done);
  if(EFI_ERROR(Status))
    return Status;
  if(t->error)
    return EFI_DEVICE_ERROR;
  gBS->CopyMem(ucsi_message, &t->message, sizeof(*ucsi_message));
  return EFI_SUCCESS;
}

static void onreceive(struct glh_descriptor* glhd, struct glink_hdr* data, UINTN size){
  struct glink_ucsi* this = glhd->p.private;
  if(data->owner != MSG_OWNER_UCSI)
    return;
  size -= sizeof(struct glink_hdr);
  if(data->type == MSG_TYPE_REQ_RESP){
    if(data->opcode == OP_UCSI_READ){
      if(this->state == CMD_READ_IN_TRANSIT){
        if(this->transaction_in_progress && size){
          // copying only version, reserved, cci, message_in
          // not copying control, message_out
          gBS->CopyMem(&this->transaction_fifo_start->message, (void*)(data+1), size > 8 ? 8 : size);
          if(size > 16)
            gBS->CopyMem(&this->transaction_fifo_start->message.message_in, (void*)(data+1)+16, size-16 > 16 ? 16 : size-16);
        }
        set_state(this, CMD_READ_DONE);
      }else{
        DEBUG((EFI_D_ERROR, "ucsi: got UCSI_READ response, but not in this->state CMD_READ_IN_TRANSIT! Current this->state: %a\n", cmd_state_name[this->state]));
      }
    }else if(data->opcode == OP_UCSI_WRITE){
      if(this->state == CMD_WRITE_IN_TRANSIT){
        set_state(this, CMD_WRITE_SENT);
      }else if(this->state == CMD_ACK_IN_TRANSIT){
        set_state(this, CMD_ACK_DONE);
      }else{
        DEBUG((EFI_D_ERROR, "ucsi: got UCSI_WRITE response, but not in this->state CMD_WRITE_IN_TRANSIT or CMD_ACK_IN_TRANSIT! Current this->state: %a\n", cmd_state_name[this->state]));
      }
    }
  }else if(data->type == MSG_TYPE_NOTIFY && data->opcode == UCSI_NOTIFICATION){
    struct ucsi_notification*restrict notification = (struct ucsi_notification*)(data+1);
    DEBUG((EFI_D_WARN, "ucsi: notification: %lX\n", notification->cci));
    int changed_connector = CCI_get_connector_change_indicator(notification->cci);
    if(changed_connector){
      // We do that after we've read a UCSI_GET_CONNECTOR_STATUS command. If we ack it early, we'll loose the status change bits.
      // Although, we won't actually use those anyway.
      // this->ack_required |= UCSI_ACK_CONNECTOR_CHANGE;
      BitmapSet(this->connector_changed_set, changed_connector);
    }
    if(notification->cci & CCI_BIT_command_completed){
      this->ack_required |= UCSI_ACK_COMMAND_COMPLETE;
      if(this->state == CMD_WRITE_SENT){
        set_state(this, CMD_WRITE_DONE);
      }else{
        DEBUG((EFI_D_WARN, "ucsi: got UCSI_ACK_COMMAND_COMPLETE message, but not in this->state CMD_WRITE_SENT! Current this->state: %a\n", cmd_state_name[this->state]));
      }
    }
    if(notification->cci & CCI_BIT_error){
      if(!this->error_notification_received)
        DEBUG((EFI_D_ERROR, "ucsi: receiver ERROR notification!\n"));
      this->error_notification_received = TRUE;
    }
    this->work_pending = TRUE;
    gBS->SignalEvent(this->state_change_event);
  }
}

EFI_STATUS glink_ucsi_create(struct glink_ucsi** ret, EFI_HANDLE handle, const char* xport, const char* remote, const char* channel_name){
  EFI_STATUS Status = 0;
  struct glink_ucsi* this = 0;
  Status = gBS->AllocatePool(EfiBootServicesData, sizeof(*this), (VOID**)&this);
  if(EFI_ERROR(Status)){
    DEBUG ((EFI_D_WARN, "glink_ucsi_create: AllocatePool failed\n"));
    return EFI_DEVICE_ERROR;
  }
  gBS->SetMem(this, sizeof(*this), 0);
  this->handle = handle;
  this->transaction_fifo_end = &this->transaction_fifo_start;
  this->temp_transaction.glink_ucsi = this;
  this->temp_transaction.acknowledged = TRUE;
  this->temp_transaction.done = TRUE;
  Status = gBS->CreateEvent(
    EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
    state_change_callback, this, &this->state_change_event
  );
  if(EFI_ERROR(Status)){
    DEBUG ((EFI_D_ERROR, "glink_ucsi_create: Failed to create this->state_change_event! Status = %r\n", Status));
    return EFI_DEVICE_ERROR;
  }
  Status = gBS->CreateEvent(
    EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
    timeout_callback, this, &this->timeout_event
  );
  if(EFI_ERROR(Status)){
    DEBUG ((EFI_D_ERROR, "glink_ucsi_create: Failed to create this->state_change_event! Status = %r\n", Status));
    return EFI_DEVICE_ERROR;
  }
  this->glink = mGlinkHelperProtocol->open(xport, remote, channel_name, &(const struct glh_open_params){
    .onreceive = onreceive,
    .private = this,
  });
  if(!this->glink){
    DEBUG ((EFI_D_WARN, "mGlinkHelperProtocol->open failed\n"));
    return EFI_DEVICE_ERROR;
  }
  this->work_pending = TRUE;
  poll(this, &this->init_done);
  if(!this->init_done){
    DEBUG ((EFI_D_ERROR, "glink_ucsi_create: init failed! Status = %r\n", Status));
    return EFI_DEVICE_ERROR;
  }
  *ret = this;
  return EFI_SUCCESS;
}

/*UCSI_PROTOCOL ucsi_protocol = {
  .Open = glink_ucsi_create,
  .Close = glink_ucsi_destroy,
  .CreateTransactionSync = create_transaction_sync,
  .CreateTransactionAsync = create_transaction_async,
  .WriteAsync = ucsi_write_async,
  .WriteSync = ucsi_write_sync,
  .Read = ucsi_read_sync,
};*/
