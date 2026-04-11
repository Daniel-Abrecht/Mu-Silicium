#ifndef GLINK_HELPER_H
#define GLINK_HELPER_H

#define MSG_OWNER_CHARGER 0x800A
#define MSG_TYPE_REQ_RESP 1
#define MSG_TYPE_NOTIFY   2

#define MSG_OP_SET_NOTIFY_REQ 0x04
#define MSG_OP_CHARGER_USB_PROPERTY_GET 0x32
#define MSG_OP_CHARGER_USB_PROPERTY_SET 0x33

typedef struct glh_open_params glh_open_params_t;
typedef struct glh_descriptor glh_descriptor_t;
typedef struct glh_channel glh_channel_t;
typedef struct glh_link glh_link_t;

typedef struct glink_handle glink_handle_t; // Opaque type

struct glink_hdr;

struct glh_open_params {
  void* private;
  void (*onreceive)(struct glh_descriptor* descriptor, struct glink_hdr* data, UINTN size);
};

struct glh_descriptor {
  const glh_channel_t* channel;
  struct glh_open_params p;
};

struct glh_channel {
  glink_handle_t* handle;
  const char* channel_name;
  const glh_link_t* link;
  volatile BOOLEAN is_channel_open; // Should probably rather be an atomic, but not sure the uefi runtime supports those.
};

struct glh_link {
  glink_handle_t* link_handle;
  const char* xport;
  const char* remote;
  volatile BOOLEAN is_link_up; // Should probably rather be an atomic, but not sure the uefi runtime supports those.
};



struct glink_hdr {
  UINT32 owner;
  UINT32 type;
  UINT32 opcode;
};



typedef struct GLINK_HELPER_PROTOCOL_ {
  glh_descriptor_t* (*EFIAPI open)(const char* xport, const char* remote, const char* channel_name, const struct glh_open_params* initial);
  void (*EFIAPI close)(struct glh_descriptor* dp);
  EFI_STATUS (*EFIAPI send_sync)(struct glh_descriptor* d, const struct glink_hdr* data, UINTN size);
  EFI_STATUS (*EFIAPI poll)(struct glh_descriptor* d, UINT64 timeout_us, volatile BOOLEAN* done);
  EFI_STATUS (*EFIAPI charger_send_sync)(
    glh_descriptor_t* d,
    const struct glink_hdr* request, UINTN request_size,
    struct glink_hdr* response, UINTN* response_size
  );
  EFI_STATUS (*charger_usb_set_property)(struct glh_descriptor* d, UINT32 property, UINT32 value);
  EFI_STATUS (*charger_usb_get_property)(struct glh_descriptor* d, UINT32 property, UINT32* value);
  EFI_STATUS (*charger_enable_notifications)(struct glh_descriptor* d);
} GLINK_HELPER_PROTOCOL;

#endif
