#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include "glink.h"
#include "glink-helper.h"

#define RECEIVE_PACKET_QUEUE_COUNT 6
#define MAX_RECEIVE_PACKET_SIZE    0x1000
#define RETRY_TIME     10 * 1000
#define WAIT_TIMEOUT 2000 * 1000

extern EFI_GUID gGlinkHelperProtocolGuid;
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

static UINT64 wait_link(struct link_full* link, UINT64 timeout_us){
  // FIXME: Actually measure the time. There are functions for getting a time or counter, but they seam a pain to deal with.
  // Arduino has the millis() function, it's easy to reason about, retuning a single integer with known unit, and the only
  // overflow to worry about being that of the integer type. But there seams to be nothing like that in UEFI!?!
  UINT64 elapsed = 0;
  while(TRUE){
    glink_error_t error = 0;
    enum glink_link_state link_state = 0;
    EFI_STATUS status = mGlinkProtocol->poll_link_state(link->public.link_handle, &link_state, &error);
    if(!EFI_ERROR(status) && error == 0 && link_state == GLINK_LINK_STATE_UP && link->public.is_link_up){
      DEBUG((EFI_D_WARN, "glink::wait_link: link is up\n"));
      return timeout_us-elapsed;
    }
    if(elapsed >= timeout_us-RETRY_TIME)
      break;
    gBS->Stall(RETRY_TIME);
    elapsed += RETRY_TIME;
  }
  DEBUG((EFI_D_ERROR, "glink::wait_link: link did not come up!\n"));
  return FALSE;
}

static UINT64 wait_channel(struct channel_full* ch, UINT64 timeout_us){
  if(!ch->public.link->is_link_up)
    if(!wait_link(BASE_CR(ch->public.link, struct link_full, public), timeout_us))
      return FALSE;
  // FIXME: Actually measure the time. There are functions for getting a time or counter, but they seam a pain to deal with.
  // Arduino has the millis() function, it's easy to reason about, retuning a single integer with known unit, and the only
  // overflow to worry about being that of the integer type. But there seams to be nothing like that in UEFI!?!
  UINT64 elapsed = 0;
  while(TRUE){
    glink_error_t error = 0;
    EFI_STATUS status = mGlinkProtocol->poll_receive_queue(ch->public.handle, &error);
    if(!EFI_ERROR(status) && error == 0 && ch->public.is_channel_open){
      DEBUG((EFI_D_WARN, "glink::wait_channel: channel is up\n"));
      return timeout_us-elapsed;
    }
    if(elapsed >= timeout_us-RETRY_TIME)
      break;
    gBS->Stall(RETRY_TIME);
    elapsed += RETRY_TIME;
  }
  DEBUG((EFI_D_ERROR, "glink::wait_channel: channel did not come up!\n"));
  return FALSE;
}

static struct link_full* link_list;

static void link_put(struct link_full* l){
  if(l->channel_list)
    return;
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
      return;
    }
  }
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
  glink_error_t error = 0;
  if(EFI_ERROR(mGlinkProtocol->open(&config, &ch->public.handle, &error) || error)){
    DEBUG((EFI_D_ERROR, "glink::open\n"));
    goto error_open;
  }
  if(!wait_channel(ch, WAIT_TIMEOUT))
    goto error_channel;
  ch->next = *pch;
  *pch = ch;
  return ch;
error_channel:
  if(EFI_ERROR(mGlinkProtocol->close(ch->public.handle, &error) || error)){
    DEBUG((EFI_D_ERROR, "glink::close failed for xport %a remote %a channel %a: 0x%X\n", ch->public.link->xport, ch->public.link->remote, ch->public.channel_name, error));
    link_put(l);
    return 0;
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
      return ch;
    }
    return create_channel(pch, l, channel_name);
  }
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
  glink_error_t error = 0;
  if(EFI_ERROR(mGlinkProtocol->link_register(&config, l, &error) || error)){
    DEBUG((EFI_D_ERROR, "glink::link_register failed: 0x%X\n", error));
    gBS->FreePool(l);
    return 0;
  }
  l->public.xport  = xport;
  l->public.remote = remote;
  l->public.link_handle = config.handle;
  l->next = *pl;
  *pl = l;
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
  d->next = ch->descriptor_list;
  ch->descriptor_list = d;
  return &d->public;
}

static void EFIAPI glink_helper_close(struct glh_descriptor* dp){
  struct descriptor_full* d = BASE_CR(dp, struct descriptor_full, public);
  struct channel_full* ch = BASE_CR(d->public.channel, struct channel_full, public);
  for(struct descriptor_full** pd=&ch->descriptor_list; *pd; pd=&(*pd)->next){
    if(*pd == d){
      *pd = d->next;
      break;
    }
  }
  if(ch->descriptor_list)
    return;
  struct link_full* l = BASE_CR(ch->public.link, struct link_full, public);
  for(struct channel_full** pch=&l->channel_list; *pch; pch=&(*pch)->next){
    if(*pch == ch){
      *pch = ch->next;
      break;
    }
  }
  glink_error_t error = 0;
  if(EFI_ERROR(mGlinkProtocol->close(ch->public.handle, &error) || error)){
    DEBUG((EFI_D_ERROR, "glink::close failed for xport %a remote %a channel %a: 0x%X\n", ch->public.link->xport, ch->public.link->remote, ch->public.channel_name, error));
    return;
  }
  gBS->FreePool(ch);
  link_put(l);
}

static void EFIAPI onlink(struct glink_link_info* info, void* priv){
  struct link_full* l = priv;
  DEBUG((EFI_D_WARN, "Glink onlink: xport: \"%a\", remote: \"%a\", state: %d\n", l->public.xport, l->public.remote, info->state));
  l->public.is_link_up = info->state == GLINK_LINK_STATE_UP;
}

static void EFIAPI onreceive(glink_handle_t* handle, void* priv_open, void* priv_receive_intent, void* data, UINTN size, UINTN intent_used){
  struct channel_full* ch = priv_open;
  DEBUG((EFI_D_WARN, "Glink onreceive: \"%a\", remote: \"%a\", channel: \"%a\", size: %d\n",
    ch->public.link->xport, ch->public.link->remote, ch->public.channel_name,
    size
  ));
  for(struct descriptor_full* it=ch->descriptor_list; it; it=it->next)
    if(it->public.p.onreceive)
      it->public.p.onreceive(&it->public, data, size);
  glink_error_t error = 0;
  if(EFI_ERROR(mGlinkProtocol->receive_done(handle, data, TRUE, &error) || error))
    DEBUG((EFI_D_ERROR, "glink::receive_done failed: %d\n", error));
}

static void EFIAPI onsenddone(glink_handle_t* handle, void* priv_open, void* priv_write, void* data, UINTN size){
  struct channel_full* ch = priv_open;
  DEBUG((EFI_D_WARN, "Glink onsenddone: xport: \"%a\", remote: \"%a\", channel: \"%a\", size: %d\n",
    ch->public.link->xport, ch->public.link->remote, ch->public.channel_name,
    size
  ));
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
    for(int i=0; i<RECEIVE_PACKET_QUEUE_COUNT; i++)
      if(EFI_ERROR(mGlinkProtocol->queue_receive_intent(handle, NULL, MAX_RECEIVE_PACKET_SIZE, &error) || error))
        DEBUG((EFI_D_ERROR, "glink::queue_receive_intent failed: %d\n", error));
  }
}

static EFI_STATUS EFIAPI glink_helper_poll(struct glh_descriptor* d){
  return wait_channel(BASE_CR(d->channel, struct channel_full, public), WAIT_TIMEOUT) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

static EFI_STATUS EFIAPI glink_helper_send_sync(struct glh_descriptor* d, const void* data, UINTN size){
  struct channel_full* ch = BASE_CR(d->channel, struct channel_full, public);
  UINTN id = ++(ch->seq);
  UINT32 elapsed = 0; // FIXME: Actually measure the time.
  while(TRUE){
    glink_error_t error = 0;
    EFI_STATUS status = mGlinkProtocol->send(d->channel->handle, (void*)id, data, size, 0, &error);
    if(!EFI_ERROR(status) && error == 0)
      break;
    if(elapsed > WAIT_TIMEOUT-RETRY_TIME)
      goto error_timeout;
    gBS->Stall(RETRY_TIME);
    elapsed += RETRY_TIME;
    mGlinkProtocol->poll_receive_queue(ch->public.handle, &error);
  }
  while(TRUE){
    glink_error_t error = 0;
    mGlinkProtocol->poll_receive_queue(ch->public.handle, &error);
    if(ch->ack-id < (((UINTN)1)<<(sizeof(UINTN)*8-1)))
      break; // onsenddone was called for this send call. (so long as ch->ack < id it'll overflow)
    gBS->Stall(RETRY_TIME);
    elapsed += RETRY_TIME;
  }
  return EFI_SUCCESS;
error_timeout:
  return EFI_DEVICE_ERROR;
}

GLINK_HELPER_PROTOCOL mGlinkHelperProtocol = {
  .open = glink_helper_open,
  .close = glink_helper_close,
  .send_sync = glink_helper_send_sync,
  .poll = glink_helper_poll,
};

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
  return EFI_SUCCESS;
error:
  return Status;
}
