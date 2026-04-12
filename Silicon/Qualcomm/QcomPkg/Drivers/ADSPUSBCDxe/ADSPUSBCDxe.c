#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/GlinkHelper.h>
#include "adsp.h"

typedef struct _EFI_PIL_PROTOCOL {
  UINT64 Revision;
  EFI_STATUS (EFIAPI *ProcessPilImage) (IN CHAR16* Subsys);
} EFI_PIL_PROTOCOL;


extern EFI_GUID gEfiPilProtocolGuid;
static EFI_PIL_PROTOCOL* mPILProtocol;

extern EFI_GUID gGlinkHelperProtocolGuid;
static GLINK_HELPER_PROTOCOL* mGlinkHelperProtocol;
static glh_descriptor_t* glhd;

static EFI_EVENT ProcessNotificationsEvt;

static const char* notification_array[] = {
#define X(N, V) [V-FIRST_NOTIFICATION] = #N,
  NOTIFICATION_LIST(X) // See adsp.h
#undef X
};


static BOOLEAN power_is_on;

static void connector_power_on(){
  EFI_STATUS Status = 0;
  if(power_is_on) return;
  DEBUG((EFI_D_WARN, "turn power on\n"));
  power_is_on = 1;
  Status = mGlinkHelperProtocol->charger_usb_set_property(glhd, USB_OTG_VBUS_REGULATOR_ENABLE, 1);
  if(EFI_ERROR(Status)){
    DEBUG ((EFI_D_WARN, "Setting USB_OTG_VBUS_REGULATOR_ENABLE=1 failed: %r\n", Status));
    return;
  }
}

static void connector_power_off(){
  EFI_STATUS Status = 0;
  if(!power_is_on) return;
  DEBUG((EFI_D_WARN, "turn power off\n"));
  Status = mGlinkHelperProtocol->charger_usb_set_property(glhd, USB_OTG_VBUS_REGULATOR_ENABLE, 0);
  if(EFI_ERROR(Status)){
    DEBUG ((EFI_D_WARN, "Setting USB_OTG_VBUS_REGULATOR_ENABLE=0 failed: %r\n", Status));
    return;
  }
  power_is_on = 0;
}

struct bitset256 {
  UINT64 value[4];
};
static void bitset256_clear(struct bitset256*restrict set){
  *set = (struct bitset256){0};
}
static void bitset256_set(struct bitset256*restrict set, UINT8 value){
  set->value[value/64] |= 1<<(value % 64);
}
static BOOLEAN bitset256_get(struct bitset256*restrict set, UINT8 value){
  return !!(set->value[value/64] & (1<<(value % 64)));
}

struct bitset256 notification_set;

static UINT32 cid_status = 0;
static UINT32 typec_mode = 0;
static UINT32 power_on_off = 0;

STATIC VOID EFIAPI ProcessNotifications(IN EFI_EVENT Event, IN VOID *Context){
  EFI_STATUS Status = 0;
  if(!glhd) return;

  struct bitset256 set = notification_set;
  bitset256_clear(&notification_set);
  //DEBUG((EFI_D_WARN, "%016X %016X %016X %016X\n", set.value[0], set.value[1], set.value[2], set.value[3]));
  
  if( bitset256_get(&set, CHARGER_N_CID_DETECT)
   || bitset256_get(&set, CHARGER_N_OTG_ENABLE)
   || bitset256_get(&set, CHARGER_N_OTG_DISABLE)
  ){ // update_typec_state already calls update_cid_detect
    Status = mGlinkHelperProtocol->charger_usb_get_property(glhd, USB_CID_STATUS, &cid_status);
    DEBUG((EFI_D_WARN, "cid_status: %d\n", cid_status));
  }
  if( bitset256_get(&set, CHARGER_N_TYPEC_STATE_CHANGE)
   || bitset256_get(&set, CHARGER_N_OTG_ENABLE)
   || bitset256_get(&set, CHARGER_N_OTG_DISABLE)
  ){
    Status = mGlinkHelperProtocol->charger_usb_get_property(glhd, USB_TYPEC_MODE, &typec_mode);
    DEBUG((EFI_D_WARN, "typec_mode: %d\n", typec_mode));
  }
  if(bitset256_get(&set, CHARGER_N_OTG_ENABLE)){ // Note: this isn't about the data role
    power_on_off = TRUE;
    DEBUG((EFI_D_WARN, "OTG_ENABLE\n"));
  }
  if(bitset256_get(&set, CHARGER_N_OTG_DISABLE)){
    power_on_off = FALSE;
    DEBUG((EFI_D_WARN, "OTG_DISABLE\n"));
  }
  if( cid_status      // Cable is connected
   && typec_mode == 1 // power role: 0=DRP, 1=SNK, 2=SRC
   && power_on_off    // Power is to be turned on
  ){
    connector_power_on();
  }else{
    connector_power_off();
  }
}

void onreceive(struct glh_descriptor* glhd, struct glink_hdr* data, UINTN size){
  // WARNING: You can't use glink functions in this callback!
  // If you must use one of them, then you need to defer it using an event.

  if(data->type == MSG_TYPE_NOTIFY){
    DEBUG((EFI_D_ERROR, "onreceive notify: 0x%X 0x%X 0x%X", data->owner, data->type, data->opcode));
    if(data->owner == MSG_OWNER_CHARGER && data->opcode == 0x07){
      UINT32 notification = *(UINT32*)(data+1);
      const char* name = 0;
      if(notification>=FIRST_NOTIFICATION && notification<CHARGER_N_UNKNOWN)
        name = notification_array[notification-FIRST_NOTIFICATION];
      if(!name) name = "";
      DEBUG((EFI_D_ERROR, " 0x%X %a\n", notification, name));
    }else{
      DEBUG((EFI_D_ERROR, "\n"));
    }
  }
  
  if(data->owner == MSG_OWNER_CHARGER && data->type == MSG_TYPE_NOTIFY && data->opcode == 0x07){
    UINT32 notification = *(UINT32*)(data+1);
    if(notification <= 0xFF){
      bitset256_set(&notification_set, notification);
      gBS->SignalEvent(ProcessNotificationsEvt);
    }
  }
}

VOID EFIAPI ExitBootServices(IN EFI_EVENT Event, IN VOID *Context);

EFI_STATUS EFIAPI Main(
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status = EFI_SUCCESS;

  Status = gBS->LocateProtocol(&gEfiPilProtocolGuid, NULL, (VOID **)&mPILProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate PIL Protocol! Status = %r\n", Status));
    goto error;
  }

  Status = gBS->LocateProtocol (&gGlinkHelperProtocolGuid, NULL, (VOID *)&mGlinkHelperProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate GlinkHelper Protocol! Status = %r\n", Status));
    goto error;
  }

  {
    static EFI_EVENT ExitEvt;
    Status = gBS->CreateEvent(EVT_SIGNAL_EXIT_BOOT_SERVICES, TPL_NOTIFY, ExitBootServices, NULL, &ExitEvt);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "CreateEvent for EVT_SIGNAL_EXIT_BOOT_SERVICES failed! Status = %r\n", Status));
      goto error;
    }
  }
  
  Status = mPILProtocol->ProcessPilImage(L"FULL_ADSP");
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "ProcessPilImage Failed! Status = %r\n", Status));
    goto error;
  }

  Status = gBS->CreateEvent(EVT_NOTIFY_SIGNAL, TPL_CALLBACK, ProcessNotifications, NULL, &ProcessNotificationsEvt);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "CreateEvent Failed! Status = %r\n", Status));
    goto error;
  }

  glhd = mGlinkHelperProtocol->open("SMEM", "lpass", "PMIC_RTR_ADSP_APPS", &(const struct glh_open_params){
    .onreceive = onreceive,
  });
  if(!glhd){
    DEBUG ((EFI_D_WARN, "mGlinkHelperProtocol->open failed\n"));
    goto error;
  }


  Status = mGlinkHelperProtocol->charger_enable_notifications(glhd);
  DEBUG ((EFI_D_WARN, "glink_helper_charger_enable_notifications: %r\n", Status));

  Status = mGlinkHelperProtocol->charger_usb_set_property(glhd, USB_OTG_AP_ENABLE, 1);
  DEBUG ((EFI_D_WARN, "USB_OTG_AP_ENABLE: %r\n", Status));
  Status = mGlinkHelperProtocol->charger_usb_set_property(glhd, USB_OEM_MISC_CTL, 0x51);
  DEBUG ((EFI_D_WARN, "USB_OEM_MISC_CTL: %r\n", Status));
  Status = mGlinkHelperProtocol->charger_usb_set_property(glhd, USB_TYPEC_SINKONLY, 0);
  DEBUG ((EFI_D_WARN, "USB_TYPEC_SINKONLY: %r\n", Status));
/*  Status = mGlinkHelperProtocol->charger_usb_set_property(glhd, USB_TYPEC_MODE, 0); // 0=DRP, 1=SNK, 2=SRC
  DEBUG ((EFI_D_WARN, "USB_TYPEC_MODE: %r\n", Status));
  Status = mGlinkHelperProtocol->charger_usb_set_property(glhd, USB_CCDETECT_HAPPENED, 1);
  DEBUG ((EFI_D_WARN, "USB_CCDETECT_HAPPENED: %r\n", Status));
  gBS->Stall(100*1000);
  Status = mGlinkHelperProtocol->charger_usb_set_property(glhd, USB_OTG_SWITCH, 1);
  DEBUG ((EFI_D_WARN, "USB_OTG_SWITCH: %r\n", Status));*/
  // Status = mGlinkHelperProtocol->charger_usb_set_property(glhd, USB_OTG_BOOST_CURRENT, 1000); // current limit in mA. Default value is 0, not sure what that means.
  // DEBUG ((EFI_D_WARN, "USB_OTG_BOOST_CURRENT: %r\n", Status));
  // Status = mGlinkHelperProtocol->charger_usb_set_property(glhd, USB_OTG_VBUS_REGULATOR_ENABLE, 1);
  // DEBUG ((EFI_D_WARN, "USB_OTG_VBUS_REGULATOR_ENABLE: %r\n", Status));

  // mGlinkHelperProtocol->close(glhd);


  return EFI_SUCCESS;
error:
  return Status;
}

VOID EFIAPI ExitBootServices(IN EFI_EVENT Event, IN VOID *Context) {
  if(glhd)
    mGlinkHelperProtocol->close(glhd);
}

