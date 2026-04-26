#ifndef UCSI_H
#define UCSI_H

#include <Library/DebugLib.h>

#define UCSI_NOTIFICATION 0x13

#define UCSI_ACK_CONNECTOR_CHANGE (1<<16)
#define UCSI_ACK_COMMAND_COMPLETE (1<<17)

#define CCI_BIT_end_of_message_indicator(CCI)   (1<<0)
#define CCI_get_connector_change_indicator(CCI) (((CCI)>>1)&0x7F)
#define CCI_get_data_length(CCI)                (((CCI)>>8)&0xFF)
#define CCI_BIT_security_request    (1<<23)
#define CCI_BIT_fw_update_request   (1<<24)
#define CCI_BIT_not_supported       (1<<25)
#define CCI_BIT_cancel_completed    (1<<26)
#define CCI_BIT_reset_completed     (1<<27)
#define CCI_BIT_busy                (1<<28)
#define CCI_BIT_acknowledge_command (1<<29)
#define CCI_BIT_error               (1<<30)
#define CCI_BIT_command_completed   (1<<31)

enum {
  UCSI_PPM_RESET = 0x01,
  UCSI_CANCEL = 0x02,
  UCSI_CONNECTOR_RESET = 0x03,
  UCSI_ACK_CC_CI = 0x04,
  UCSI_SET_NOTIFICATION_ENABLE = 0x05,
  UCSI_GET_CAPABILITY = 0x06,
  UCSI_GET_CONNECTOR_CAPABILITY = 0x07,
  UCSI_SET_CCOM = 0x08,
  UCSI_SET_UOR = 0x09,
  // UCSI_SET_PDM = 0x0A, // obsolete and unimplemented
  UCSI_SET_PDR = 0x0B,
  UCSI_GET_ALTERNATE_MODES = 0x0C,
  UCSI_GET_CAM_SUPPORTED = 0x0D,
  UCSI_GET_CURRENT_CAM = 0x0E,
  UCSI_SET_NEW_CAM = 0x0F,
  UCSI_GET_PDOS = 0x10,
  UCSI_GET_CABLE_PROPERTY = 0x11,
  UCSI_GET_CONNECTOR_STATUS = 0x12,
  UCSI_GET_ERROR_STATUS = 0x13,
  UCSI_SET_POWER_LEVEL = 0x14,
  UCSI_GET_PD_MESSAGE = 0x15,
  UCSI_GET_ATTENTION_VDO = 0x16,
  // 0x17
  UCSI_GET_CAM_CS = 0x18,
  UCSI_LPM_FW_UPDATE_REQUEST = 0x19,
  UCSI_SECURITY_REQUEST = 0x1A,
  UCSI_SET_RETIMER_MODE = 0x1B,
  UCSI_SET_SINK_PATH = 0x1C,
};

struct ucsi_data {
  UINT16 version; // 8 major, 4 minor, 4 patch
  UINT16 reserved;
  UINT32 cci;
  UINT64 control;
  UINT8 message_in [0x10];
  UINT8 message_out[0x10];
};
_Static_assert(sizeof(struct ucsi_data) == 0x30, "UCSI data structure had unexpected size");

enum control_flags__set_notification_enable {
  UCSI_SN_COMMAND_COMPLETED = 1<<16, // (R)
  UCSI_SN_EXTERNAL_SUPPLY_CHANGE = 1<<17, // (O)
  UCSI_SN_POWER_OPERATION_MODE_CHANGE = 1<<18, // (R)
  UCSI_SN_ATTENTION = 1<<19, // (O)
  UCSI_SN_LPM_FW_UPDATE_REQUEST_FROM_PORT_PARTNER = 1<<20, // (O)
  UCSI_SN_SUPPORTED_PROVIDER_CAPABILITIES_CHANGE = 1<<21, // (O)
  UCSI_SN_NEGOTIATED_POWER_LEVEL_CHANGE = 1<<22, // (O)
  UCSI_SN_PD_RESET_COMPLETE = 1<<23, // (O)
  UCSI_SN_SUPPORTED_CAM_CHANGE = 1<<24, // (O)
  UCSI_SN_BATTERY_CHARGING_STATUS_CHANGE = 1<<25, // (R)
  UCSI_SN_SECURITY_REQUEST_FROM_PORT_PARTNER = 1<<26, // (O)
  UCSI_SN_CONNECTOR_PARTNER_CHANGE = 1<<27, // (R)
  UCSI_SN_POWER_DIRECTION_CHANGE = 1<<28, // (R)
  UCSI_SN_SET_RETIMER_MODE = 1<29, // (O)
  UCSI_SN_CONNECT_CHANGE = 1<<30, // (R)
  UCSI_SN_ERROR = 1<<31, // (R)
  UCSI_SN_SINK_PATH_STATUS_CHANGE = 1<<32, //(R)
};

struct ucsi_notification {
  UINT32 cci; // cci
  UINT32 receiver;
  UINT32 reserved;
};

struct get_connector_status_in {
  UINT16 connector_status_change;                  //  0 -  15

  UINT16 power_operation_mode : 3;                 // 16 -  18
  UINT16 connect_status  : 1;                      // 19
  UINT16 power_direction : 1;                      // 20
  UINT16 connector_partner_flags : 8;              // 21 -  28
  UINT16 connector_partner_type : 3;               // 29 -  31

  UINT32 request_data_object;                      // 32 -  63

  UINT32 battery_charging_capability_status : 2;   // 64 -  65
  UINT32 provider_capabilities_limited_reason : 4; // 66 -  69
  UINT32 bcd_pd_version_operation_mode : 16;       // 70 -  85
  UINT32 orientation : 1;                          // 86
  UINT32 sink_path_status : 1;                     // 87
  UINT32 reverse_current_protection_status : 1;    // 88
  UINT32 reserved : 7;                             // 89 -  95

  UINT32 reserved_2;                               // 96 - 128
};
// We fill it in with bit shifts. At worst, it's going to be a bit less efficient.
//_Static_assert(sizeof(struct get_connector_status_in) == 0x10, "get_connector_status_in data structure had unexpected size");

//  GET_ERROR_STATUS::error_information
#define UCSI_ESI_UNRECOGNIZED_COMMAND (1<<0)
#define UCSI_ESI_NON_EXISTENT_CONNECTOR_NUMBER (1<<1)
#define UCSI_ESI_INVALID_COMMAND_SPECIFIC_PARAMETERS (1<<2)
#define UCSI_ESI_INCOMPATIBLE_CONNECTOR_PARTNER (1<<3)
#define UCSI_ESI_CC_COMMUNICATION_ERROR (1<<4)
#define UCSI_ESI_COMMAND_UNSUCCESSFUL_DUE_TO_DEAD_BATTERY_CONDITION (1<<5)
#define UCSI_ESI_CONTRACT_NEGOTIATION_FAILURE (1<<6)
#define UCSI_ESI_OVERCURRENT (1<<7)
#define UCSI_ESI_UNDEFINED (1<<8)
#define UCSI_ESI_PORT_PARTNER_REJECTED_SWAP (1<<9)
#define UCSI_ESI_HARD_RESET (1<<10)
#define UCSI_ESI_PPM_POLICY_CONFLICT (1<<11)
#define UCSI_ESI_SWAP_REJECTED (1<<12)
#define UCSI_ESI_REVERSE_CURRENT_PROTECTION (1<<13)
#define UCSI_ESI_SET_SINK_PATH_REJECTED (1<<14)

typedef struct ucsi_transaction_async ucsi_transaction_async_t;
typedef struct ucsi_transaction_sync  ucsi_transaction_sync_t;

EFI_STATUS ucsi_write_async(ucsi_transaction_async_t* t, const struct ucsi_data* ucsi_message);
EFI_STATUS ucsi_write_sync(ucsi_transaction_sync_t* t, const struct ucsi_data* ucsi_message);
EFI_STATUS ucsi_read_sync(ucsi_transaction_sync_t* t, struct ucsi_data* ucsi_message);

#endif
