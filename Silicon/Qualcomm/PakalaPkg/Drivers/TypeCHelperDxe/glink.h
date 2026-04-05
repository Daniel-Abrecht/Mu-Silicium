#ifndef GLINK_H
#define GLINK_H

typedef struct glink_handle glink_handle_t; // Opaque type
typedef int glink_error_t;

enum glink_channel_state {
  GLINK_CHANNEL_CONNECTED,
  GLINK_CHANNEL_LOCAL_DISCONNECTED,
  GLINK_CHANNEL_REMOTE_DISCONNECTED,
};

enum glink_link_state {
 GLINK_LINK_STATE_UP,
 GLINK_LINK_STATE_DOWN,
};

struct glink_link_info {
  const char* xport;
  const char* remote;
  enum glink_link_state state;
};

struct glink_link {
  int version;
  const char* xport;
  const char* remote;
  void (EFIAPI* onlink)(struct glink_link_info* link, void* priv);
  glink_handle_t* handle;
};

struct glink_channel {
  const char* xport;
  const char* remote;
  const char* channel_name;
  unsigned    options; 
  const void* priv;
  void (EFIAPI* onreceive)(glink_handle_t* handle, void* priv_open, void* priv_receive_intent, void* data, UINTN size, UINTN intent_used);
  void* _1;
  void* _2;
  void (EFIAPI* onsenddone)(glink_handle_t* handle, void* priv_open, void* priv_write, void* data, UINTN size);
  void (EFIAPI* onstatechange)(glink_handle_t* handle, void* priv_open, enum glink_channel_state event);
  void* _3;
  void* _4;
  void* _5;
  void* _6;
  void* _7;
  unsigned remote_intent_timeout;
  void* _9;
  void* _10;
};


typedef struct _GLINK_PROTOCOL {
  UINT64 Revision;
  EFI_STATUS (EFIAPI* link_register)(struct glink_link* link, void* priv, glink_error_t* ret_error);
  EFI_STATUS (EFIAPI* link_deregister)(glink_handle_t* handle, glink_error_t* ret_error);
  EFI_STATUS (EFIAPI* open)(struct glink_channel* config, glink_handle_t** handle, glink_error_t* ret_error);
  EFI_STATUS (EFIAPI* close)(glink_handle_t* handle, glink_error_t* ret_error);
  EFI_STATUS (EFIAPI* send)(glink_handle_t* handle, void* priv, const void* data, UINTN size, UINT32 options, glink_error_t* ret_error);
  void* _1;
  EFI_STATUS (EFIAPI* queue_receive_intent)(glink_handle_t* handle, void* priv, UINTN size, glink_error_t* ret_error); // internally prepares an additional receive buffer
  EFI_STATUS (EFIAPI* receive_done)(glink_handle_t* handle, const void* data, BOOLEAN reuse, glink_error_t* ret_error); // we are done with the receive buffer, it can now be reused
  void* _2;
  void* _3;
  void* _4;
  void* _5;
  void* _6;
  void* _7;
  EFI_STATUS (EFIAPI* poll_link_state)(glink_handle_t* handle, enum glink_link_state* ret_state, glink_error_t* ret_error);
  EFI_STATUS (EFIAPI* poll_receive_queue)(glink_handle_t* handle, glink_error_t* ret_error);
} GLINK_PROTOCOL;

#endif
