#ifndef GLINK_HELPER_H
#define GLINK_HELPER_H

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
  BOOLEAN is_channel_open;
};

struct glh_link {
  glink_handle_t* link_handle;
  const char* xport;
  const char* remote;
  BOOLEAN is_link_up;
};

typedef struct GLINK_HELPER_PROTOCOL_ {
  glh_descriptor_t* (*EFIAPI open)(const char* xport, const char* remote, const char* channel_name, const struct glh_open_params* initial);
  void (*EFIAPI close)(struct glh_descriptor* dp);
  EFI_STATUS (*EFIAPI send_sync)(struct glh_descriptor* d, const void* data, UINTN size);
  EFI_STATUS (*EFIAPI poll)(struct glh_descriptor* d);
} GLINK_HELPER_PROTOCOL;

#endif
