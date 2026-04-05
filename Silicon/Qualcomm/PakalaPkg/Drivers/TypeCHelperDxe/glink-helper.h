#ifndef GLINK_HELPER_H
#define GLINK_HELPER_H

#define MSG_OWNER_CHARGER 0x800A
#define MSG_TYPE_REQ_RESP 1
#define MSG_TYPE_NOTIFY   2
#define MSG_OP_CHARGER_USB_STATUS_GET 0x32
#define MSG_OP_CHARGER_USB_STATUS_SET 0x33


typedef struct glh_open_params glh_open_params_t;
typedef struct glh_descriptor glh_descriptor_t;
typedef struct glh_channel glh_channel_t;
typedef struct glh_link glh_link_t;

typedef struct glink_handle glink_handle_t; // Opaque type

struct glh_open_params {
  void* private;
  void (*onreceive)(struct glh_descriptor* descriptor, void* data, UINTN size);
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

struct battery_charger_request_msg {
  struct glink_hdr hdr;
  UINT32 battery_id;
  UINT32 property_id;
  UINT32 value;
};

struct battery_charger_response_msg {
  struct glink_hdr hdr;
  UINT32 property_id;
  UINT8 data[];
};



typedef struct GLINK_HELPER_PROTOCOL_ {
  glh_descriptor_t* (*EFIAPI open)(const char* xport, const char* remote, const char* channel_name, const struct glh_open_params* initial);
  void (*EFIAPI close)(struct glh_descriptor* dp);
  EFI_STATUS (*EFIAPI send_sync)(struct glh_descriptor* d, const void* data, UINTN size);
  EFI_STATUS (*EFIAPI poll)(struct glh_descriptor* d, UINT64 timeout_us, volatile BOOLEAN* done);
  EFI_STATUS (*EFIAPI charger_send_sync)(
    glh_descriptor_t* d, UINT32 opcode, UINT32 property, UINT32 value,
    void* response, UINTN* response_size
  );
} GLINK_HELPER_PROTOCOL;


#endif
