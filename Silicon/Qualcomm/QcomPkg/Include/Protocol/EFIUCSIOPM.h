#ifndef __UCSI_OPM_PROTOCOL_H__
#define __UCSI_OPM_PROTOCOL_H__

typedef struct EFI_UCSI_OPM_PROTOCOL_INTERFACE_ EFI_UCSI_OPM_PROTOCOL_INTERFACE;
typedef struct EFI_UCSI_OPM_PROTOCOL_ EFI_UCSI_OPM_PROTOCOL;
typedef struct EFI_UCSI_OPM_CONNECTOR_PROTOCOL_INTERFACE_ EFI_UCSI_OPM_CONNECTOR_PROTOCOL_INTERFACE;
typedef struct EFI_UCSI_OPM_CONNECTOR_PROTOCOL_ EFI_UCSI_OPM_CONNECTOR_PROTOCOL;

struct ucsi_data {
  UINT16 version; // 8 major, 4 minor, 4 patch
  UINT16 reserved;
  UINT32 cci;
  UINT64 control;
  UINT8 message_in [0x10];
  UINT8 message_out[0x10];
};
_Static_assert(sizeof(struct ucsi_data) == 0x30, "UCSI data structure had unexpected size");

typedef struct ucsi_transaction_async ucsi_transaction_async_t;
typedef struct ucsi_transaction_sync  ucsi_transaction_sync_t;

typedef ucsi_transaction_sync_t*  ucsi_create_sync_transaction_t(EFI_UCSI_OPM_PROTOCOL* ucsi);
typedef ucsi_transaction_async_t* ucsi_create_async_transaction_t(
  EFI_UCSI_OPM_PROTOCOL* controller,
  void* userdata,
  void(*ontransactiondone)(void* userdata, const struct ucsi_data* data, EFI_STATUS error, UINT16 ucsi_error_status)
);
typedef void ucsi_destroy_async_transaction_t(ucsi_transaction_async_t*);
typedef void ucsi_destroy_sync_transaction_t(ucsi_transaction_sync_t*);

typedef EFI_STATUS ucsi_write_async_t(ucsi_transaction_async_t* t, const struct ucsi_data* ucsi_message);
typedef EFI_STATUS ucsi_write_sync_t(ucsi_transaction_sync_t* t, const struct ucsi_data* ucsi_message);
typedef EFI_STATUS ucsi_read_sync_t(ucsi_transaction_sync_t* t, struct ucsi_data* ucsi_message);

typedef struct EFI_UCSI_OPM_PROTOCOL_INTERFACE_ {
  ucsi_create_sync_transaction_t* create_sync_transaction;
  ucsi_create_async_transaction_t* create_async_transaction;
  ucsi_destroy_sync_transaction_t* destroy_sync_transaction;
  ucsi_destroy_async_transaction_t* destroy_async_transaction;

  ucsi_write_async_t* write_async;
  ucsi_write_sync_t* write_sync;
  ucsi_read_sync_t* read_sync;
} EFI_UCSI_OPM_PROTOCOL_INTERFACE;

typedef struct EFI_UCSI_OPM_PROTOCOL_ {
  const EFI_UCSI_OPM_PROTOCOL_INTERFACE* I;
  const struct ucsi_get_capability_in* capability;
} EFI_UCSI_OPM_PROTOCOL;

typedef struct EFI_UCSI_OPM_CONNECTOR_PROTOCOL_ {
  const EFI_UCSI_OPM_CONNECTOR_PROTOCOL_INTERFACE* I;
  EFI_UCSI_OPM_PROTOCOL* controller;
  UINT8 index; // 1 relative, not 0 relative!
  struct ucsi_get_connector_status_in* connector_status;
} EFI_UCSI_OPM_CONNECTOR_PROTOCOL;

#endif
