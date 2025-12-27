#ifndef TLMM_H
#define TLMM_H

/**
 * bias:
 *   0 no pull down
 *   1 pull down
 *   2 keeper
 *   3 pull up
 * 
 * drive: 0..7. drive*2+2 = mA
 */

#define TLMM_GPIO(gpio, func, is_output, bias, drive) ( \
    ((gpio ) & 0x3FF) << 4 | \
    ((func ) & 0xF  )      | \
    ((is_output) & 0x1) << 14 | \
    ((bias ) & 0x3) << 15  | \
    ((drive) & 0xF) << 17  | \
    0x20000000 \
  )

typedef EFI_STATUS (EFIAPI *EFI_TLMM_GPIO_CONFIG)(UINT32 config, UINT32 enable);
typedef EFI_STATUS (EFIAPI *EFI_TLMM_GPIO_ARRAY_CONFIG)(UINT32 enable, UINT32 *config_group, UINT32 size);
typedef EFI_STATUS (EFIAPI *EFI_TLMM_GPIO_READ)(UINT32 config, UINT32 *value);
typedef EFI_STATUS (EFIAPI *EFI_TLMM_GPIO_WRITE)(UINT32 config, UINT32 value);
typedef EFI_STATUS (EFIAPI *EFI_TLMM_SET_INACTIVE_CONFIG)(UINT32 gpio_number, UINT32 config);


typedef struct _EFI_TLMM_PROTOCOL {
  UINT64                       Revision;
  EFI_TLMM_GPIO_CONFIG         GpioConfig;
  EFI_TLMM_GPIO_ARRAY_CONFIG   GpioArrayConfig;
  EFI_TLMM_GPIO_READ           GpioRead;
  EFI_TLMM_GPIO_WRITE          GpioWrite;
  EFI_TLMM_SET_INACTIVE_CONFIG SetInactiveConfig;
} EFI_TLMM_PROTOCOL;

#endif
