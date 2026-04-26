#include <Library/PcdLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/Glink.h>
#include <Protocol/GlinkHelper.h>

// Notes
// The PcdGlinkPollWorkaround can be used in case IPCCDxe / interrupts, dont work correctly. It polls the glink channel
// in a timer.
// The TPL is raised around every Glink call, this is not pretty, but there were some races in GlinkDxe before that.
// They certainly happen if the glink poll function is called from a timer. They may also happen when interrupts from
// IPCCDxe are received, but I currently have no way to test that.

#define RECEIVE_PACKET_QUEUE_COUNT 6
#define MAX_RECEIVE_PACKET_SIZE    0x1000
#define RETRY_TIME     10
#define WAIT_TIMEOUT 2000

extern EFI_GUID gGlinkHelperProtocolGuid;
static GLINK_HELPER_PROTOCOL mGlinkHelperProtocol;

static EFI_EVENT PollEvt; // Workaround for when interrupts / IPCCDxe doesn't work.

extern EFI_GUID gGlinkProtocolGuid;
GLINK_PROTOCOL* mGlinkProtocol;

static void EFIAPI onlink(struct glink_link_info* link, void* priv);
static void EFIAPI onreceive(glink_handle_t* handle, void* priv_open, void* priv_receive_intent, void* data, UINTN size, UINTN intent_used);
static void EFIAPI onsenddone(glink_handle_t* handle, void* priv_open, void* priv_write, void* data, UINTN size);
static void EFIAPI onstatechange(glink_handle_t* handle, void* priv_open, enum glink_channel_state event);

struct descriptor_full {
  glh_descriptor_t public;
  struct descriptor_full* next;
};

struct channel_full {
  glh_channel_t public;
  UINTN refcount;
  struct channel_full* next;
  struct descriptor_full* descriptor_list;
  UINTN seq, ack;
};

struct link_full {
  glh_link_t public;
  struct link_full* next;
  struct channel_full* channel_list;
};

struct battery_charger_response_wait_list {
  struct battery_charger_response_wait_list* next;
  const struct glink_hdr* request;
  UINTN request_size;
  struct glink_hdr* response;
  UINTN response_size;
  volatile BOOLEAN done;
};

static UINT64 glink_helper_poll_internal(struct channel_full* ch, UINT64 timeout_ms, volatile BOOLEAN* done){
  if(!timeout_ms)
    timeout_ms = WAIT_TIMEOUT;
  if(timeout_ms <= RETRY_TIME)
    timeout_ms = RETRY_TIME+1;
  // FIXME: Actually measure the time. There are functions for getting a time or counter, but they seam a pain to deal with.
  // Arduino has the millis() function, it's easy to reason about, retuning a single integer with known unit, and the only
  // overflow to worry about being that of the integer type. But there seams to be nothing like that in UEFI!?!
  UINT64 elapsed = 0;
  while(TRUE){
    glink_error_t error = 0;
    EFI_TPL  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
    EFI_STATUS Status = mGlinkProtocol->poll_receive_queue(ch->public.handle, &error);
    gBS->RestoreTPL (OldTpl);
    if(!EFI_ERROR(Status) && error == 0 && *done)
      return timeout_ms-elapsed;
    if(elapsed >= timeout_ms-RETRY_TIME)
      break;
    gBS->Stall(RETRY_TIME*1000);
    elapsed += RETRY_TIME;
  }
  return FALSE;
}


static UINT64 wait_link(struct link_full* link, UINT64 timeout_ms){
  // FIXME: Actually measure the time. There are functions for getting a time or counter, but they seam a pain to deal with.
  // Arduino has the millis() function, it's easy to reason about, retuning a single integer with known unit, and the only
  // overflow to worry about being that of the integer type. But there seams to be nothing like that in UEFI!?!
  UINT64 elapsed = 0;
  while(TRUE){
    glink_error_t error = 0;
    enum glink_link_state link_state = 0;
    EFI_TPL  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
    EFI_STATUS Status = mGlinkProtocol->poll_link_state(link->public.link_handle, &link_state, &error);
    gBS->RestoreTPL (OldTpl);
    if(!EFI_ERROR(Status) && error == 0 && link_state == GLINK_LINK_STATE_UP && link->public.is_link_up){
      DEBUG((EFI_D_WARN, "glink::wait_link: link is up\n"));
      return timeout_ms-elapsed;
    }
    if(elapsed >= timeout_ms-RETRY_TIME)
      break;
    gBS->Stall(RETRY_TIME*1000);
    elapsed += RETRY_TIME;
  }
  DEBUG((EFI_D_ERROR, "glink::wait_link: link did not come up!\n"));
  return FALSE;
}

static UINT64 wait_channel(struct channel_full* ch, UINT64 timeout_ms){
  if(!ch->public.link->is_link_up)
    if(!wait_link(BASE_CR(ch->public.link, struct link_full, public), timeout_ms))
      return FALSE;
  UINT64 x = glink_helper_poll_internal(ch, timeout_ms, &ch->public.is_channel_open);
  if(x){
    DEBUG((EFI_D_WARN, "glink::wait_channel: channel is up\n"));
  }else{
    DEBUG((EFI_D_ERROR, "glink::wait_channel: channel did not come up!\n"));
  }
  return x;
}


static struct link_full* link_list;

////// Workaround for IPPC / Interupt problems

static void firstLinkInit(void){
  EFI_STATUS Status = 0;
  if(FixedPcdGetBool(PcdGlinkPollWorkaround)){
    Status = gBS->SetTimer(PollEvt, TimerPeriodic, 100000);
    if(EFI_ERROR(Status))
      DEBUG ((EFI_D_ERROR, "GlinkHelper: SetTimer: TimerPeriodic failed! Status = %r\n", Status));
    DEBUG ((EFI_D_WARN, "GlinkHelper: Poll timer started\n", Status));
  }
}

static void lastLinkCleanup(void){
  EFI_STATUS Status = 0;
  if(FixedPcdGetBool(PcdGlinkPollWorkaround)){
    Status = gBS->SetTimer(PollEvt, TimerCancel, 0);
    if(EFI_ERROR(Status))
      DEBUG ((EFI_D_ERROR, "GlinkHelper: SetTimer: TimerCancel failed! Status = %r\n", Status));
    DEBUG ((EFI_D_WARN, "GlinkHelper: Poll timer stopped\n", Status));
  }
}

static BOOLEAN Poll_repoll;
STATIC VOID EFIAPI Poll(IN EFI_EVENT Event, IN VOID *Context){
  for(struct link_full* l=link_list; l; l=l->next){
    for(struct channel_full* ch=l->channel_list; ch; ch=ch->next){
      for(int i=0; i<32; i++){
        Poll_repoll = FALSE;
        glink_error_t error = 0;
        EFI_STATUS Status = mGlinkProtocol->poll_receive_queue(ch->public.handle, &error);
        if(EFI_ERROR(Status) || error)
          DEBUG((EFI_D_WARN, "Glink::poll_receive_queue failed: %r %d\n", Status, error));
        if(!Poll_repoll)
          break;
      }
    }
  }
}

//////

// TODO: There could still be a race when calling open/close in events,
// mainly when the last entry is removed before a new one is added but after the link / channel was found.
// This would need a proper refcount.

static void link_put(struct link_full* l){
  EFI_TPL  OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
  if(l->channel_list){
    gBS->RestoreTPL (OldTpl);
    return;
  }
  for(struct link_full** pl=&link_list; *pl; pl=&(*pl)->next){
    if(*pl == l){
      *pl = l->next;
      break;
    }
  }
  if(l->public.link_handle){
    glink_error_t error = 0;
    if(EFI_ERROR(mGlinkProtocol->link_deregister(l->public.link_handle, &error) || error)){
      DEBUG((EFI_D_ERROR, "glink::link_deregister failed for xport %a remote %a: 0x%X\n", l->public.xport, l->public.remote, error));
      gBS->RestoreTPL (OldTpl);
      return;
    }
  }
  if(!link_list)
    lastLinkCleanup();
  gBS->RestoreTPL (OldTpl);
  gBS->FreePool(l);
}

static struct channel_full* create_channel(struct channel_full** pch, struct link_full* l, const char* channel_name){
  struct channel_full* ch;
  if(EFI_ERROR(gBS->AllocatePool(EfiBootServicesData, sizeof(*ch), (VOID**)&ch))){
    DEBUG((EFI_D_ERROR, "AllocatePool failed\n"));
    goto error_alloc;
  }
  gBS->SetMem(ch, sizeof(*ch), 0);
  ch->public.channel_name = channel_name;
  ch->public.link = &l->public;
  struct glink_channel config = {
    .channel_name = channel_name,
    .xport = l->public.xport,
    .remote = l->public.remote,
    .onreceive = onreceive,
    .onsenddone = onsenddone,
    .onstatechange = onstatechange,
    .priv = ch,
  };
  if(!l->public.is_link_up)
    if(!wait_link(l, WAIT_TIMEOUT))
      goto error_open;
  {
    glink_error_t error = 0;
    EFI_TPL OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
    EFI_STATUS Status = mGlinkProtocol->open(&config, &ch->public.handle, &error);
    gBS->RestoreTPL (OldTpl);
    if(EFI_ERROR(Status) || error){
      DEBUG((EFI_D_ERROR, "glink::open\n"));
      goto error_open;
    }
  }
  if(!wait_channel(ch, WAIT_TIMEOUT))
    goto error_channel;
  {
    EFI_TPL  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
    ch->next = *pch;
    *pch = ch;
    gBS->RestoreTPL (OldTpl);
  }
  return ch;
error_channel:
  {
    glink_error_t error = 0;
    EFI_TPL OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
    EFI_STATUS Status = mGlinkProtocol->close(ch->public.handle, &error);
    gBS->RestoreTPL (OldTpl);
    if(EFI_ERROR(Status) || error){
      DEBUG((EFI_D_ERROR, "glink::close failed for xport %a remote %a channel %a: 0x%X\n", ch->public.link->xport, ch->public.link->remote, ch->public.channel_name, error));
      link_put(l);
      return 0;
    }
  }
error_open:
  gBS->FreePool(ch);
error_alloc:
  link_put(l);
  return 0;
}

static struct channel_full* EFIAPI glink_helper_open_sub(
  const char* xport,
  const char* remote,
  const char* channel_name
){
  EFI_TPL  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  struct link_full** pl;
  for(pl=&link_list; *pl; pl=&(*pl)->next){
    struct link_full* l = *pl;
    int    r = AsciiStrCmp(l->public.xport, xport);
    if(!r) r = AsciiStrCmp(l->public.remote, remote);
    if(r < 0) continue;
    if(r > 0) break;
    // link found
    struct channel_full** pch;
    for(pch=&l->channel_list; *pch; pch=&(*pch)->next){
      struct channel_full* ch = *pch;
      int r = AsciiStrCmp(ch->public.channel_name, channel_name);
      if(r < 0) continue;
      if(r > 0) break;
      gBS->RestoreTPL (OldTpl);
      return ch;
    }
    gBS->RestoreTPL (OldTpl);
    return create_channel(pch, l, channel_name);
  }
  gBS->RestoreTPL (OldTpl);
  struct link_full* l;
  if(EFI_ERROR(gBS->AllocatePool(EfiBootServicesData, sizeof(*l), (VOID**)&l))){
    DEBUG((EFI_D_ERROR, "AllocatePool failed\n"));
    return 0;
  }
  gBS->SetMem(l, sizeof(*l), 0);
  struct glink_link config = {
    .version = 1,
    .xport  = xport,
    .remote = remote,
    .onlink = onlink,
  };
  {
    glink_error_t error = 0;
    EFI_TPL  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
    EFI_STATUS Status = mGlinkProtocol->link_register(&config, l, &error);
    gBS->RestoreTPL (OldTpl);
    if(EFI_ERROR(Status) || error){
      DEBUG((EFI_D_ERROR, "glink::link_register failed: 0x%X\n", error));
      gBS->FreePool(l);
      return 0;
    }
  }
  l->public.xport  = xport;
  l->public.remote = remote;
  l->public.link_handle = config.handle;
  {
    EFI_TPL  OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
    if(!link_list)
      firstLinkInit();
    l->next = *pl;
    *pl = l;
    gBS->RestoreTPL (OldTpl);
  }
  return create_channel(&l->channel_list, l, channel_name);
}

static struct glh_descriptor* EFIAPI glink_helper_open(
  const char* xport,
  const char* remote,
  const char* channel_name,
  const struct glh_open_params* initial
){
  struct descriptor_full* d = 0;
  if(EFI_ERROR(gBS->AllocatePool(EfiBootServicesData, sizeof(*d), (VOID**)&d))){
    DEBUG((EFI_D_ERROR, "AllocatePool failed\n"));
    return 0;
  }
  struct channel_full* ch = glink_helper_open_sub(xport, remote, channel_name);
  if(!ch){
    gBS->FreePool(d);
    return 0;
  }
  if(initial){
    d->public.p = *initial;
  }else{
    gBS->SetMem(d, sizeof(*d), 0);
  }
  d->public.channel = &ch->public;
  {
    EFI_TPL  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
    d->next = ch->descriptor_list;
    ch->descriptor_list = d;
    gBS->RestoreTPL (OldTpl);
  }
  return &d->public;
}

static void EFIAPI glink_helper_close(struct glh_descriptor* dp){
  struct descriptor_full* d = BASE_CR(dp, struct descriptor_full, public);
  struct channel_full* ch = BASE_CR(d->public.channel, struct channel_full, public);
  EFI_TPL  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  for(struct descriptor_full** pd=&ch->descriptor_list; *pd; pd=&(*pd)->next){
    if(*pd == d){
      *pd = d->next;
      break;
    }
  }
  if(ch->descriptor_list){
    gBS->RestoreTPL (OldTpl);
    return;
  }
  struct link_full* l = BASE_CR(ch->public.link, struct link_full, public);
  for(struct channel_full** pch=&l->channel_list; *pch; pch=&(*pch)->next){
    if(*pch == ch){
      *pch = ch->next;
      break;
    }
  }
  glink_error_t error = 0;
  if(EFI_ERROR(mGlinkProtocol->close(ch->public.handle, &error)) || error){
    DEBUG((EFI_D_ERROR, "glink::close failed for xport %a remote %a channel %a: 0x%X\n", ch->public.link->xport, ch->public.link->remote, ch->public.channel_name, error));
    gBS->RestoreTPL (OldTpl);
    return;
  }
  gBS->RestoreTPL (OldTpl);
  gBS->FreePool(ch);
  link_put(l);
}

static void EFIAPI onlink(struct glink_link_info* info, void* priv){
  struct link_full* l = priv;
  DEBUG((EFI_D_WARN, "Glink onlink: xport: \"%a\", remote: \"%a\", state: %d\n", l->public.xport, l->public.remote, info->state));
  l->public.is_link_up = info->state == GLINK_LINK_STATE_UP;
}

static struct battery_charger_response_wait_list* bcr_wait_list;

static void EFIAPI onreceive(glink_handle_t* handle, void* priv_open, void* priv_receive_intent, void* data, UINTN size, UINTN intent_used){
  struct channel_full* ch = priv_open;

  Poll_repoll = TRUE;
  if(size < sizeof(struct glink_hdr)){
    DEBUG((EFI_D_ERROR, "Glink onreceive: response is too short!"));
    return;
  }
  struct glink_hdr* response = data;
  EFI_TPL  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  for(struct battery_charger_response_wait_list** it=&bcr_wait_list; *it; it=&(*it)->next){
    struct battery_charger_response_wait_list* e = *it;
    const UINT32 opcode = response->opcode;
    if( e->request->owner  != response->owner
     || e->request->type   != response->type
     || e->request->opcode != opcode
    ) continue;
    if( response->owner == MSG_OWNER_CHARGER && response->type == MSG_TYPE_REQ_RESP && (
        opcode == MSG_OP_CHARGER_USB_PROPERTY_SET     || opcode == MSG_OP_CHARGER_USB_PROPERTY_GET
     || opcode == MSG_OP_CHARGER_BATTERY_PROPERTY_SET || opcode == MSG_OP_CHARGER_BATTERY_PROPERTY_GET
     || opcode == MSG_OP_CHARGER_WLS_PROPERTY_SET     || opcode == MSG_OP_CHARGER_WLS_PROPERTY_GET
    )){
      if(size < 4*5) continue; // Message is too short!
      if(((UINT32*)(e->request+1))[1] != ((UINT32*)(response+1))[0]) // comparing property_id
        continue;
    }
    e->done = TRUE;
    if(e->response_size > size)
      e->response_size = size;
    if(e->response && e->response_size)
      gBS->CopyMem(e->response, response, e->response_size);
    *it = e->next;
    break;
  }
  for(struct descriptor_full* it=ch->descriptor_list; it; it=it->next)
    if(it->public.p.onreceive)
      it->public.p.onreceive(&it->public, response, size);
  glink_error_t error = 0;
  if(EFI_ERROR(mGlinkProtocol->receive_done(handle, data, TRUE, &error) || error))
    DEBUG((EFI_D_ERROR, "glink::receive_done failed: %d\n", error));
  gBS->RestoreTPL (OldTpl);
}

static void EFIAPI onsenddone(glink_handle_t* handle, void* priv_open, void* priv_write, void* data, UINTN size){
  struct channel_full* ch = priv_open;
/*  DEBUG((EFI_D_WARN, "Glink onsenddone: xport: \"%a\", remote: \"%a\", channel: \"%a\", size: %d\n",
    ch->public.link->xport, ch->public.link->remote, ch->public.channel_name,
    size
  ));*/
  UINTN id = (UINTN)priv_write;
  if((id-ch->ack) < (((UINTN)1)<<(sizeof(UINTN)*8-1))){
    ch->ack = id;
  }else{
    DEBUG((EFI_D_WARN, "Glink onsenddone: bad priv_open or out of order!\n"));
  }
}

static void EFIAPI onstatechange(glink_handle_t* handle, void* priv_open, enum glink_channel_state state){
  struct channel_full* ch = priv_open;
  DEBUG((EFI_D_WARN, "Glink onstatechange: xport: \"%a\", remote: \"%a\", channel: \"%a\", state: %d\n",
    ch->public.link->xport, ch->public.link->remote, ch->public.channel_name, state
  ));
  ch->public.is_channel_open = state == GLINK_CHANNEL_CONNECTED;
  glink_error_t error = 0;
  if(state == GLINK_CHANNEL_CONNECTED){
    EFI_TPL  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
    for(int i=0; i<RECEIVE_PACKET_QUEUE_COUNT; i++)
      if(EFI_ERROR(mGlinkProtocol->queue_receive_intent(handle, NULL, MAX_RECEIVE_PACKET_SIZE, &error) || error))
        DEBUG((EFI_D_ERROR, "glink::queue_receive_intent failed: %d\n", error));
    gBS->RestoreTPL (OldTpl);
  }
}

static EFI_STATUS EFIAPI glink_helper_poll(struct glh_descriptor* d){
  struct channel_full* ch = BASE_CR(d->channel, struct channel_full, public);
  EFI_TPL OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  glink_error_t error = 0;
  EFI_STATUS Status = mGlinkProtocol->poll_receive_queue(ch->public.handle, &error);
  gBS->RestoreTPL (OldTpl);
  if(EFI_ERROR(Status))
    return Status;
  if(error)
    return EFI_DEVICE_ERROR;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI glink_helper_send_sync(struct glh_descriptor* d, const struct glink_hdr* data, UINTN size){
  struct channel_full* ch = BASE_CR(d->channel, struct channel_full, public);
  UINTN id = ++(ch->seq);
  UINT32 elapsed = 0; // FIXME: Actually measure the time.
  while(TRUE){
    glink_error_t error = 0;
    {
      EFI_TPL  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
      EFI_STATUS Status = mGlinkProtocol->send(d->channel->handle, (void*)id, data, size, 0, &error);
      gBS->RestoreTPL (OldTpl);
      if(!EFI_ERROR(Status) && error == 0)
        break;
    }
    if(elapsed > WAIT_TIMEOUT-RETRY_TIME)
      goto error_timeout;
    gBS->Stall(RETRY_TIME*1000);
    elapsed += RETRY_TIME;
    {
      EFI_TPL  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
      mGlinkProtocol->poll_receive_queue(ch->public.handle, &error);
      gBS->RestoreTPL (OldTpl);
    }
  }
  while(TRUE){
    {
      glink_error_t error = 0;
      EFI_TPL  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
      mGlinkProtocol->poll_receive_queue(ch->public.handle, &error);
      gBS->RestoreTPL (OldTpl);
    }
    if(ch->ack-id < (((UINTN)1)<<(sizeof(UINTN)*8-1)))
      break; // onsenddone was called for this send call. (so long as ch->ack < id it'll overflow)
    gBS->Stall(RETRY_TIME*1000);
    elapsed += RETRY_TIME;
  }
  return EFI_SUCCESS;
error_timeout:
  return EFI_DEVICE_ERROR;
}

static EFI_STATUS EFIAPI glink_helper_send_receive_sync(
  glh_descriptor_t* d,
  const struct glink_hdr* request, UINTN request_size,
  struct glink_hdr* response, UINTN* response_size
){
  if( request_size < sizeof(*request)
   || (response_size && *response_size < sizeof(*response))
  ) return EFI_INVALID_PARAMETER;
  const UINT32 opcode = response->opcode;
  if( response->owner == MSG_OWNER_CHARGER && response->type == MSG_TYPE_REQ_RESP && (
      opcode == MSG_OP_CHARGER_USB_PROPERTY_SET     || opcode == MSG_OP_CHARGER_USB_PROPERTY_GET
   || opcode == MSG_OP_CHARGER_BATTERY_PROPERTY_SET || opcode == MSG_OP_CHARGER_BATTERY_PROPERTY_GET
   || opcode == MSG_OP_CHARGER_WLS_PROPERTY_SET     || opcode == MSG_OP_CHARGER_WLS_PROPERTY_GET
  )) if(request_size < 6)
      return EFI_INVALID_PARAMETER;
  struct channel_full* ch = BASE_CR(d->channel, struct channel_full, public);
  EFI_STATUS Status = 0;
  struct battery_charger_response_wait_list object = {
    .response = response,
    .response_size = response_size ? *response_size : 0,
    .request = request,
    .request_size = request_size,
  };
  {
    EFI_TPL OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
    object.next = bcr_wait_list,
    bcr_wait_list = &object;
    gBS->RestoreTPL (OldTpl);
  }
  Status = glink_helper_send_sync(d, object.request, object.request_size);
  if(EFI_ERROR(Status)){
    DEBUG((EFI_D_ERROR, "glink_helper_send_receive_sync: send_sync failed: %r. Glink owner: %d type: %d opcode %d\n",
           Status, request->owner, request->type, request->opcode));
    goto error;
  }
  Status = glink_helper_poll_internal(ch, WAIT_TIMEOUT, &object.done);
  if(EFI_ERROR(Status)){
    DEBUG((EFI_D_ERROR, "glink_helper_send_receive_sync: poll failed: %r. Glink owner: %d type: %d opcode %d\n",
           Status, request->owner, request->type, request->opcode));
    goto error;
  }
  {
    EFI_TPL OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
    for(struct battery_charger_response_wait_list** it=&bcr_wait_list; it; it=&(*it)->next)
      if(*it == &object){ *it = object.next; break; }
    gBS->RestoreTPL (OldTpl);
  }
  if(response_size)
    *response_size = object.response_size;
  return EFI_SUCCESS;
error:
  {
    EFI_TPL OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
    for(struct battery_charger_response_wait_list** it=&bcr_wait_list; it; it=&(*it)->next)
      if(*it == &object){ *it = object.next; break; }
    gBS->RestoreTPL (OldTpl);
  }
  return Status;
}

EFI_STATUS EFIAPI glink_helper_init(
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;

  Status = gBS->LocateProtocol(&gGlinkProtocolGuid, NULL, (VOID *)&mGlinkProtocol);
  if(EFI_ERROR(Status)){
    DEBUG((EFI_D_ERROR, "Failed to Locate Glink Protocol! Status = %r\n", Status));
    goto error;
  }
  Status = gBS->InstallMultipleProtocolInterfaces( &ImageHandle,
                  &gGlinkHelperProtocolGuid, &mGlinkHelperProtocol,
                  NULL);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Install ULog Protocol! Status = %r\n", Status));
    Status = -1;
    goto error;
  }
  if(FixedPcdGetBool(PcdGlinkPollWorkaround)){
    Status = gBS->CreateEvent(
      EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
      Poll, NULL, &PollEvt
    );
    if(EFI_ERROR(Status)){
      DEBUG ((EFI_D_ERROR, "GlinkHelper: Failed to create timer event! Status = %r\n", Status));
      goto error;
    }
  }
  return EFI_SUCCESS;
error:
  return Status;
}


static GLINK_HELPER_PROTOCOL mGlinkHelperProtocol = {
  .open = glink_helper_open,
  .close = glink_helper_close,
  .send_sync = glink_helper_send_sync,
  .poll = glink_helper_poll,
  .send_receive_sync = glink_helper_send_receive_sync,
};

