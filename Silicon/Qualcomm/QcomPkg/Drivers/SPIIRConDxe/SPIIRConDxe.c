#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>



typedef struct _SpiDeviceParameters {
  int clock_mode;
  int clock_polarity;
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




EFI_STATUS
EFIAPI
SPIIRConDxeInit (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS                  Status;
  QCOM_SPI_PROTOCOL *mQcomSPIProtocol;

  int instance = 16;

  DEBUG ((EFI_D_WARN, "\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n\n"));

  // Locate Display Power State Protocol
  Status = gBS->LocateProtocol (&gQcomSPIProtocolGuid, NULL, (void**)&mQcomSPIProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate Qcom SPI Protocol! Status = %r\n", Status));
    goto end;
  }

  void* spi_handle = 0;
  Status = mQcomSPIProtocol->Open(instance, &spi_handle);
  if (Status) {
    DEBUG ((EFI_D_ERROR, "QcomSPIProtocol::Open failed = %d\n", Status));
    goto end;
  }

/*  Status = mQcomSPIProtocol->Transfer(instance, &spi_handle);
  if (Status) {
    DEBUG ((EFI_D_ERROR, "QcomSPIProtocol::Open failed = %d\n", Status));
    return Status;
  }*/

  Status = mQcomSPIProtocol->Close(spi_handle);
  if (Status) {
    DEBUG ((EFI_D_ERROR, "QcomSPIProtocol::Close failed = %d\n", Status));
    goto end;
  }

end:
  DEBUG ((EFI_D_WARN, "\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n\n"));

  return Status;
}
