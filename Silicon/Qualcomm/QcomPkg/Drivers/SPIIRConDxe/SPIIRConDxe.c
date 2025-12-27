#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/ULogCtl.h>

#include <Protocol/EFINpa.h>
#include "TLMM.h"

typedef struct _SpiDeviceParameters {
  int clock_mode;
  int clock_polarity;
  int shift_mode;
  UINT32 deassertion_time;
  UINT32 min_frequency_hz;
  UINT32 max_frequency_hz;
  int cs_polarity;
  int cs_mode;
  BOOLEAN high_speed_mode;
} SpiDeviceParameters;

typedef struct _SpiBoardInfo {
  UINT32 slave_number;
  int mode;
} SpiBoardInfo;

typedef struct _SpiTransfer {
  UINT32 num_bits_per_transfer;
  int loopback_mode;
} SpiTransfer;

typedef struct _SpiDeviceInfo {
  SpiDeviceParameters parameters;
  SpiBoardInfo board_info;
  SpiTransfer transfer;
} SpiDeviceInfo;

typedef int (EFIAPI * QCOM_SPI_PROTOCOL_OPEN)(
  int instance,
  void **handle
);
typedef int (EFIAPI * QCOM_SPI_PROTOCOL_TRANSFER) (
  void *handle,
  SpiDeviceInfo *devInfo,
  CONST UINT8 *write_buffer,
  UINT32 write_len,
  UINT8 *read_buffer,
  UINT32 read_len
);
typedef int (EFIAPI * QCOM_SPI_PROTOCOL_CLOSE)(void *spi_handle);

typedef struct _QCOM_SPI_PROTOCOL {
  UINT64 Revision;
  QCOM_SPI_PROTOCOL_OPEN     Open;
  QCOM_SPI_PROTOCOL_TRANSFER Transfer;
  QCOM_SPI_PROTOCOL_CLOSE    Close;
} QCOM_SPI_PROTOCOL;



typedef struct {
  int pull_down;
  int pwr_mode;
  int pin_controlled;
  int is_enabled;
  BOOLEAN ok;
} QCOM_VREG_Status; 

typedef EFI_STATUS (*QCOM_VREG_CONTROL)(UINT32 pmic_index, int vreg_id, BOOLEAN enable);
typedef EFI_STATUS (*QCOM_VREG_SET_LEVEL)(UINT32 pmic_index, int vreg_id, UINT32 millivolt);
typedef EFI_STATUS (*QCOM_VREG_SET_PWR_MODE)(UINT32 pmic_index, int vreg_id, int sw_mode);
typedef EFI_STATUS (*QCOM_VREG_MULTIPHASE_CTRL)(UINT32 pmic_index, int vreg_id, UINT32 phase_count);
typedef EFI_STATUS (*QCOM_VREG_SET_LEVEL_IN_MICRO_VOLT)(UINT32 pmic_index, int vreg_id, UINT32 microvolt);

typedef EFI_STATUS (*QCOM_VREG_GET_LEVEL)(UINT32 pmic_index, int vreg_id, UINT32* ret_microvolt);
typedef EFI_STATUS (*QCOM_VREG_GET_STATUS)(UINT32 PmicDeviceIndex, int vreg_id, QCOM_VREG_Status* ret_status);

typedef struct _QCOM_PMIC_VREG_PROTOCOL {
  UINT64 Revision;
  QCOM_VREG_CONTROL                 Control;
  QCOM_VREG_SET_LEVEL               SetMillivolt;
  QCOM_VREG_GET_LEVEL               GetMicrovolt;
  QCOM_VREG_SET_PWR_MODE            SetPwrMode;
  QCOM_VREG_MULTIPHASE_CTRL         MultiphaseCtrl;
  QCOM_VREG_GET_STATUS              GetStatus;
  QCOM_VREG_SET_LEVEL_IN_MICRO_VOLT SetMicrovolt;
} QCOM_PMIC_VREG_PROTOCOL;




EFI_NPA_PROTOCOL* getNpaProtocol(void){
  static EFI_NPA_PROTOCOL* mNpaProtocol;
  if(mNpaProtocol)
    return mNpaProtocol;
  EFI_STATUS Status = gBS->LocateProtocol(&gEfiNpaProtocolGuid, NULL, (void**)&mNpaProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate EFI_NPA_PROTOCOL Protocol! Status = %r\n", Status));
  }
  return mNpaProtocol;
}




// static QCOM_SPI_PROTOCOL *mQcomSPIProtocol;
static QCOM_PMIC_VREG_PROTOCOL *mQcomPmicVregProtocol;
static EFI_ULOG_CTL_PROTOCOL* mULogCtlProtocol;
static EFI_TLMM_PROTOCOL* mTlmmProtocol;

EFI_STATUS
EFIAPI
SPIIRConDxeInit (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;

  // ldob9
  // Note: The proper thing to do would probably be to lookup ldob9 in cmd_db and/or npa, but for now, this has to do.
  int regulator_pmic = 'b' - 'a';
  int regulator_index = 9  -  1 ;
  const UINT32 gpio_ir = TLMM_GPIO(53, 0, 1, 0, 2);
  
  // int spi_instance = 16;
  
  // SpiDeviceInfo dev_info = {
  //   .parameters = {
  //     .max_frequency_hz = 5000000, /* 50000000 (one more zero)? 10000000? */
  //     //.min_frequency_hz = 1000000,
  //   },
  //   .board_info = {
  //     .slave_number = 0,
  //   },
  //   .transfer = {
  //     .num_bits_per_transfer = 8,
  //   }
  // };

  DEBUG ((EFI_D_WARN, "\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n\n"));

  Status = gBS->LocateProtocol(&gULogCtlProtocolGuid, NULL, (void**)&mULogCtlProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate Qcom PMIC VREG Protocol! Status = %r\n", Status));
    goto end;
  }
  mULogCtlProtocol->EnableLog(ULOGCTL_ANY_LOG, 1);


  // Locate Qcom PMIC VREG Protocol
  Status = gBS->LocateProtocol(&gQcomPmicVregProtocolGuid, NULL, (void**)&mQcomPmicVregProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate Qcom PMIC VREG Protocol! Status = %r\n", Status));
    goto end;
  }

  Status = gBS->LocateProtocol(&gEfiTLMMProtocolGuid, NULL, (void**)&mTlmmProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate TLMM Protocol! Status = %r\n", Status));
    goto end;
  }

  EFI_NPA_PROTOCOL* mNpaProtocol = getNpaProtocol();
  if(!mNpaProtocol)
    goto end;
  
  npa_client_handle npa_ldo_mv = 0;
  Status = mNpaProtocol->CreateSyncClientEx("/pm/ldob9/mV", "SPI IR", NPA_CLIENT_REQUIRED, 0, 0, &npa_ldo_mv);
  if (EFI_ERROR (Status) || !npa_ldo_mv) {
    DEBUG ((EFI_D_ERROR, "EFI_NPA_PROTOCOL::CreateSyncClientEx for /pm/ldob9/mV failed! Status = %r\n", Status));
    goto end;
  }  

  npa_client_handle npa_ldo_mode = 0;
  Status = mNpaProtocol->CreateSyncClientEx("/pm/ldob9/mode", "SPI IR", NPA_CLIENT_REQUIRED, 0, 0, &npa_ldo_mode);
  if (EFI_ERROR (Status) || !npa_ldo_mode) {
    DEBUG ((EFI_D_ERROR, "EFI_NPA_PROTOCOL::CreateSyncClientEx for /pm/ldob9/mode failed! Status = %r\n", Status));
    goto end;
  }  

  npa_client_handle npa_ldo_en = 0;
  Status = mNpaProtocol->CreateSyncClientEx("/pm/ldob9/en", "SPI IR", NPA_CLIENT_REQUIRED, 0, 0, &npa_ldo_en);
  if (EFI_ERROR (Status) || !npa_ldo_en) {
    DEBUG ((EFI_D_ERROR, "EFI_NPA_PROTOCOL::CreateSyncClientEx for /pm/ldob9/en failed! Status = %r\n", Status));
    goto end;
  }  

  Status = mNpaProtocol->ScalarRequest(npa_ldo_mv, 2960);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "EFI_NPA_PROTOCOL::ScalarRequest for /pm/ldob9/mV failed! Status = %r\n", Status));
    goto end;
  }
  
  Status = mNpaProtocol->ScalarRequest(npa_ldo_mode, 7);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "EFI_NPA_PROTOCOL::ScalarRequest for /pm/ldob9/mode failed! Status = %r\n", Status));
    goto end;
  }

  Status = mNpaProtocol->ScalarRequest(npa_ldo_en, 1);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "EFI_NPA_PROTOCOL::ScalarRequest for /pm/ldob9/en failed! Status = %r\n", Status));
    goto end;
  }


  // DANGER: BREAKS IR, DO NOT UNCOMMENT!!!!
  //
  // Status = mTlmmProtocol->GpioConfig(gpio_ir, 1);
  // if (EFI_ERROR (Status)) {
  //   DEBUG ((EFI_D_ERROR, "TLMM::GpioConfig failed! Status = %r\n", Status));
  //   goto end;
  // }
  // 
  // Status = mTlmmProtocol->GpioWrite(gpio_ir, 1);
  // if (EFI_ERROR (Status)) {
  //   DEBUG ((EFI_D_ERROR, "TLMM::GpioWrite failed! Status = %r\n", Status));
  //   goto end;
  // }

/*
  Status = mQcomPmicGpioProtocol->CfgMode(regulator_pmic, gpio_mosi, 1); // Output
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "QcomPmicGpioProtocol::CfgMode failed! Status = %r\n", Status));
    goto end2;
  }

  Status = mQcomPmicGpioProtocol->Enable(regulator_pmic, gpio_mosi, 1); // Enable
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "QcomPmicGpioProtocol::Enable failed! Status = %r\n", Status));
    goto end2;
  }
*/

/*
  Status = mQcomPmicVregProtocol->SetMicrovolt(regulator_pmic, regulator_index, 3104000);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "QcomPmicVregProtocol::SetMicrovolt failed! Status = %r\n", Status));
    goto end2;
  }

  Status = mQcomPmicVregProtocol->SetPwrMode(regulator_pmic, regulator_index, 4);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "QcomPmicVregProtocol::SetPwrMode failed! Status = %r\n", Status));
    goto end2;
  }

  Status = mQcomPmicVregProtocol->Control(regulator_pmic, regulator_index, TRUE);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "QcomPmicVregProtocol::Control failed! Status = %r\n", Status));
    goto end2;
  }
*/

  UINT32 microvolt = 0;
  QCOM_VREG_Status vreg_status = {0};

  Status = mQcomPmicVregProtocol->GetMicrovolt(regulator_pmic, regulator_index, &microvolt);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "QcomPmicVregProtocol::GetMicrovolt failed! Status = %r\n", Status));
    goto end2;
  }

  Status = mQcomPmicVregProtocol->GetStatus(regulator_pmic, regulator_index, &vreg_status);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "QcomPmicVregProtocol::GetStatus failed! Status = %r\n", Status));
    goto end2;
  }
  DEBUG ((EFI_D_WARN, "pull_down=%d pwr_mode=%d pin_controlled=%d is_enabled=%d microvolt=%d\n",
          vreg_status.pull_down, vreg_status.pwr_mode, vreg_status.pin_controlled, vreg_status.is_enabled, microvolt));

  // Locate Qcom SPI Protocol
  // Status = gBS->LocateProtocol(&gQcomSPIProtocolGuid, NULL, (void**)&mQcomSPIProtocol);
  // if (EFI_ERROR (Status)) {
  //   DEBUG ((EFI_D_ERROR, "Failed to Locate Qcom SPI Protocol! Status = %r\n", Status));
  //   goto end2;
  // }
  // 
  // void* spi_handle = 0;
  // Status = mQcomSPIProtocol->Open(spi_instance, &spi_handle);
  // if (Status) {
  //   DEBUG ((EFI_D_ERROR, "QcomSPIProtocol::Open failed = %d\n", Status));
  //   goto end2;
  // }
  // 
  // static const UINT8 send_buf[] = "Hello World!";
  // 
  // Status = mQcomSPIProtocol->Transfer(spi_handle, &dev_info, send_buf, sizeof(send_buf), 0, 0);
  // if (Status) {
  //   DEBUG ((EFI_D_ERROR, "QcomSPIProtocol::Transfer failed = %d\n", Status));
  //   goto end2;
  // }
  // 
  // Status = mQcomSPIProtocol->Close(spi_handle);
  // if (Status) {
  //   DEBUG ((EFI_D_ERROR, "QcomSPIProtocol::Close failed = %d\n", Status));
  //   goto end2;
  // }

end2:
  // Status = mTlmmProtocol->GpioWrite(gpio_ir, 0);
  // if (EFI_ERROR (Status)) {
  //   DEBUG ((EFI_D_ERROR, "TLMM::GpioWrite failed! Status = %r\n", Status));
  //   goto end;
  // }

end:
  mULogCtlProtocol->EnableLog(ULOGCTL_ANY_LOG, 0);
  DEBUG ((EFI_D_WARN, "\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n\n"));

  return Status;
}
