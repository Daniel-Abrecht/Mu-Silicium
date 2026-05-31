#include "common.h"
#include "phy_power.h"
#include <Library/IoLib.h>


// We use magic numbers for most register offsets and values.
// In case you neet to know what these actually stand for, the linux kernel can serve as documentation for this.
// Paths will be provided in comments indicating the files there containing named constants for such numbers.

// Note: These should be sorrted by offset
#define REG_BLOCKS \
  X(COMMON, 0x0000) /* drivers/phy/qualcomm/phy-qcom-qmp-dp-com-v3.h */ \
  X(USB_SERIALIZER_DESERIALIZER, 0x1000) /* drivers/phy/qualcomm/phy-qcom-qmp-qserdes-com-v8.h */ \
  /* drivers/phy/qualcomm/phy-qcom-qmp-qserdes-txrx-v8.h */ \
  X(USB_LANE_A_TRANSMIT, 0x1400) \
  X(USB_LANE_A_RECEIVE, 0x1600)  \
  X(USB_LANE_B_TRANSMIT, 0x1800)  \
  X(USB_LANE_B_RECEIVE, 0x1A00)  \
  X(USB_PHYSICAL_CODING_SUBLAYER_MISC, 0x1C00) /* drivers/phy/qualcomm/phy-qcom-qmp-pcs-misc-v3.h */ \
  X(USB_PHYSICAL_CODING_SUBLAYER, 0x1E00) /* drivers/phy/qualcomm/phy-qcom-qmp-pcs-v8.h */ \
  X(USB_PHYSICAL_CODING_SUBLAYER_USB, 0x2100) /* drivers/phy/qualcomm/phy-qcom-qmp-pcs-v8.h */ \
  X(DISPLAYPORT_SERIALIZER_DESERIALIZER, 0x3000) /* drivers/phy/qualcomm/phy-qcom-qmp-qserdes-com-v6.h */ \
  /* drivers/phy/qualcomm/phy-qcom-qmp-qserdes-txrx-v6.h */ \
  X(DISPLAYPORT_LANE_A_TRANSMIT, 0x3400)  \
  X(DISPLAYPORT_LANE_B_TRANSMIT, 0x3800)  \
  X(DISPLAYPORT_PHY, 0x3C00) /* drivers/phy/qualcomm/phy-qcom-qmp-dp-phy.h */ \

enum reg_block {
#define X(NAME, OFFSET) REG_BLOCK_ ## NAME,
  REG_BLOCKS
#undef X
};
enum reg_block_offset {
#define X(NAME, OFFSET) REG_BLOCK_OFFSET_ ## NAME = OFFSET,
  REG_BLOCKS
#undef X
};
const UINT16 reg_block_offset_list[] = {
#define X(NAME, ...) [REG_BLOCK_ ## NAME] = REG_BLOCK_OFFSET_ ## NAME,
  REG_BLOCKS
#undef X
};
#define X(...) +1
enum { reg_block_count = REG_BLOCKS };
#undef X


struct phy_init_entry {
  UINT16 offset;
  UINT8  value;
};

struct phy_init_block {
  UINT16 sequence_count;
  const struct phy_init_entry* sequence;
};

#define INIT_ENTRY_END {0xFFFF, 0}


#define COMMON_PHY_MODE_CTRL 0x000
#define COMMON_SW_RESET 0x004
#define COMMON_POWER_DOWN_CTRL 0x008
#define COMMON_SOFTWARE_INTERRUPT_CTRL 0x00C
#define COMMON_TYPEC_CTRL 0x010
#define COMMON_TYPEC_PWRDN_CTRL 0x014
#define COMMON_RESET_OVRD_CTRL 0x01C

#define USB_PHYSICAL_CODING_SUBLAYER_SW_RESET 0x000
#define USB_PHYSICAL_CODING_SUBLAYER_START_CTRL 0x044
#define USB_PHYSICAL_CODING_SUBLAYER_STATUS 0x014
#define USB_PHYSICAL_CODING_SUBLAYER_POWER_DOWN_CONTROL 0x040

#define USB_PHYSICAL_CODING_SUBLAYER_USB_AUTONOMOUS_MODE_CTRL 0x008
#define USB_PHYSICAL_CODING_SUBLAYER_USB_LFPS_RXTERM_IRQ_CLEAR 0x014

#define DISPLAYPORT_SERIALIZER_DESERIALIZER_RESETSM_CNTRL 0x118
#define DISPLAYPORT_SERIALIZER_DESERIALIZER_C_READY_STATUS 0x2F0
#define DISPLAYPORT_SERIALIZER_DESERIALIZER_CMN_STATUS 0x2C8
#define DISPLAYPORT_SERIALIZER_DESERIALIZER_BIAS_EN_CLKBUFLR_EN 0x0DC

#define DISPLAYPORT_TRANSMIT_POL_INV 0x05C
#define DISPLAYPORT_TRANSMIT_DRV_LVL 0x014
#define DISPLAYPORT_TRANSMIT_EMP_POST1_LVL 0x00C
#define DISPLAYPORT_TRANSMIT_DRVR_EN 0x058
#define DISPLAYPORT_TRANSMIT_TRANSCEIVER_BIAS_EN 0x054



const struct phy_init_entry init_sequence_COMMON[] = {
  INIT_ENTRY_END
};

// The values assigned to the registers are pretty much the same ones the linux kernel uses (they have to be), but we initialize most things just once initially.
// We do not reinitialize registers whose values don't need to change when enabling / disabling usb. We also do not power anything down, though.

const struct phy_init_entry init_sequence_USB_SERIALIZER_DESERIALIZER[] = {
  // See drivers/phy/qualcomm/phy-qcom-qmp-qserdes-com-v8.h
  {0x000, 0xC0}, {0x004, 0x01}, {0x010, 0x02}, {0x014, 0x16},
  {0x018, 0x36}, {0x01C, 0x04}, {0x020, 0x16}, {0x024, 0x41},
  {0x028, 0x41}, {0x02C, 0x00}, {0x030, 0x55}, {0x034, 0x75},
  {0x038, 0x01}, {0x03C, 0x01}, {0x048, 0x25}, {0x04C, 0x02},
  {0x050, 0x5C}, {0x054, 0x0F}, {0x058, 0x5C}, {0x05C, 0x0F},
  {0x060, 0xC0}, {0x064, 0x01}, {0x070, 0x02}, {0x074, 0x16},
  {0x078, 0x36}, {0x080, 0x08}, {0x084, 0x1A}, {0x088, 0x41},
  {0x08C, 0x00}, {0x090, 0x55}, {0x094, 0x75}, {0x098, 0x01},
  {0x0A8, 0x25}, {0x0AC, 0x02}, {0x0BC, 0x0A}, {0x0C0, 0x01},
  {0x0CC, 0x62}, {0x0D0, 0x02}, {0x0E8, 0x0C}, {0x110, 0x1A},
  {0x124, 0x14}, {0x140, 0x04}, {0x170, 0x20}, {0x174, 0x16},
  {0x1A4, 0xB6}, {0x1A8, 0x4A}, {0x1AC, 0x36}, {0x1B4, 0x0C},
  INIT_ENTRY_END
};

// drivers/phy/qualcomm/phy-qcom-qmp-qserdes-txrx-v8.h
#define INIT_SEQUENCE_USB_LANE_TRANSMIT \
  {0x034, 0x00}, {0x038, 0x00}, {0x03C, 0x1F}, {0x040, 0x09}, \
  {0x084, 0xF5}, {0x08C, 0x11}, {0x090, 0x31}, {0x094, 0x5F}, \
  {0x0A4, 0x12}

const struct phy_init_entry init_sequence_USB_LANE_A_TRANSMIT[] = {
  INIT_SEQUENCE_USB_LANE_TRANSMIT,
  {0x0E4, 0x21},
  INIT_ENTRY_END
};

const struct phy_init_entry init_sequence_USB_LANE_A_RECEIVE[] = {
  // drivers/phy/qualcomm/phy-qcom-qmp-qserdes-txrx-v8.h
  {0x008, 0x0A}, {0x014, 0x06}, {0x030, 0x2F}, {0x034, 0x7F},
  {0x03C, 0xFF}, {0x040, 0x0F}, {0x044, 0x99}, {0x04C, 0x08},
  {0x050, 0x08}, {0x054, 0x00}, {0x058, 0x0A}, {0x060, 0x20},
  {0x0D4, 0x54}, {0x0D8, 0x0F}, {0x0DC, 0x13}, {0x0EC, 0x0E},
  {0x0F0, 0x4A}, {0x0F4, 0x0A}, {0x0F8, 0x07}, {0x0FC, 0x00},
  {0x110, 0x27},
  {0x118, 0x0C}, {0x11C, 0x04}, {0x124, 0x0E}, {0x15C, 0x3F},
  {0x160, 0xBF}, {0x164, 0xFF}, {0x168, 0xDF}, {0x16C, 0xED},
  {0x170, 0x19}, {0x174, 0x09}, {0x178, 0x91}, {0x17C, 0xB7},
  {0x180, 0xAA}, {0x1A0, 0x04}, {0x1A4, 0x38}, {0x1A8, 0x0C},
  {0x1B0, 0x10}, {0x1E4, 0x14}, {0x1F8, 0x08},
  INIT_ENTRY_END
};

const struct phy_init_entry init_sequence_USB_LANE_B_TRANSMIT[] = {
  INIT_SEQUENCE_USB_LANE_TRANSMIT,
  {0x0E4, 0x05},
  INIT_ENTRY_END
};

#define init_sequence_USB_LANE_B_RECEIVE init_sequence_USB_LANE_A_RECEIVE

const struct phy_init_entry init_sequence_USB_PHYSICAL_CODING_SUBLAYER_MISC[] = {
  INIT_ENTRY_END
};

const struct phy_init_entry init_sequence_USB_PHYSICAL_CODING_SUBLAYER[] = {
  // drivers/phy/qualcomm/phy-qcom-qmp-pcs-v8.h
  {0x0C4, 0xC4}, {0x0C8, 0x89}, {0x0CC, 0x20}, {0x0D8, 0x13},
  {0x0DC, 0x21}, {0x188, 0x55}, {0x190, 0xE7}, {0x194, 0x03},
  {0x1B0, 0x0A}, {0x1C0, 0x88}, {0x1C4, 0x13}, {0x1D0, 0x0C},
  {0x1DC, 0x4B}, {0x1EC, 0x10},
  INIT_ENTRY_END
};

const struct phy_init_entry init_sequence_USB_PHYSICAL_CODING_SUBLAYER_USB[] = {
  // drivers/phy/qualcomm/phy-qcom-qmp-pcs-usb-v8.h
  {0x018, 0xF8}, {0x03C, 0x07}, {0x040, 0x40}, {0x044, 0x00},
  INIT_ENTRY_END
};

const struct phy_init_entry init_sequence_DISPLAYPORT_SERIALIZER_DESERIALIZER[] = {
  // drivers/phy/qualcomm/phy-qcom-qmp-qserdes-com-v6.h
  {0x17C, 0x15}, {0x110, 0x3B}, {0x0E4, 0x02}, {0x0E0, 0x0C},
  {0x0E8, 0x06}, {0x164, 0x30}, {0x0F4, 0x0F}, {0x078, 0x36},
  {0x074, 0x16}, {0x070, 0x06}, {0x090, 0x00}, {0x174, 0x12},
  {0x0A0, 0x3F}, {0x0A4, 0x00}, {0x140, 0x00}, {0x0BC, 0x0A},
  {0x07C, 0x14}, {0x13C, 0x00}, {0x0DC, 0x17}, {0x170, 0x0F},
  INIT_ENTRY_END
};

const struct phy_init_entry init_sequence_DISPLAYPORT_LANE_A_TRANSMIT[] = {
  // drivers/phy/qualcomm/phy-qcom-qmp-qserdes-txrx-v6.h
  {0x0C8, 0x40}, {0x020, 0x30}, {0x02C, 0x3B}, {0x008, 0x0F},
  {0x01C, 0x03}, {0x0C0, 0x0F}, {0x060, 0x00}, {0x0C4, 0x00},
  {0x03C, 0x0C}, {0x040, 0x0C}, {0x024, 0x04},
  INIT_ENTRY_END
};

#define init_sequence_DISPLAYPORT_LANE_B_TRANSMIT init_sequence_DISPLAYPORT_LANE_A_TRANSMIT

const struct phy_init_entry init_sequence_DISPLAYPORT_PHY[] = {
  INIT_ENTRY_END
};


#define X(NAME, ...) \
  enum { INIT_SEQUENCE_COUNT_ ## NAME = sizeof(init_sequence_ ## NAME) / sizeof(*init_sequence_ ## NAME) -1 };
REG_BLOCKS
#undef X

const struct phy_init_block init_sequence[] = {
#define X(NAME, ...) \
  [REG_BLOCK_##NAME] = { \
    .sequence_count = INIT_SEQUENCE_COUNT_ ## NAME, \
    .sequence = init_sequence_ ## NAME, \
  },
REG_BLOCKS
#undef X
};

enum ss_lane_usage {
  SS_LU_DISABLED = 0,
  SS_LU_USB = 1<<0,
  SS_LU_DP  = 1<<1,
  SS_LU_USB_DP = SS_LU_USB|SS_LU_DP,
};

struct QmpComboPhy {
  UINTN reg_base;
  BOOLEAN orientation; // 0=normal, 1=reversed
  enum ss_lane_usage lane_usage;
  unsigned phy_power_config_index;
};

UINT8 CheckedMmioWrite8(UINTN off, UINT8 val){
  UINT8 old = MmioRead8(off);
  UINT8 rval = MmioWrite8(off, val);
  gBS->Stall(100);
  UINT8 readback = MmioRead8(off);
  if(val != rval || val != readback){
    DEBUG((EFI_D_WARN, "Value changed after read back! 0x%lX: %02X -> =%02X -> %02X\n", (long)off, old, val, readback));
  }else{
    if(old != val){
      DEBUG((EFI_D_WARN, "OK! 0x%lX: %02X -> %02X\n", (long)off, old, val));
    }else{
      DEBUG((EFI_D_WARN, "OK! 0x%lX: %02X: Unchanged\n", (long)off, val));
    }
  }
  // gBS->Stall(100000);
  return rval;
}
#define MmioWrite8 CheckedMmioWrite8

void phy_init(struct QmpComboPhy* phy){
  if(phy->phy_power_config_index > usb_power_config_count)
    return;
  EFI_STATUS Status;
  DEBUG((EFI_D_WARN, "PhyPowerWrapperInit\n"));
  Status = PhyPowerWrapperInit(usb_power_config[phy->phy_power_config_index]);
  if(EFI_ERROR(Status)){
    DEBUG((EFI_D_WARN, "PhyPowerWrapperInit failed!\n"));
    return;
  }
  DEBUG((EFI_D_WARN, "PhyPowerWrapperPowerOn\n"));
  Status = PhyPowerWrapperPowerOn(usb_power_config[phy->phy_power_config_index]);
  if(EFI_ERROR(Status)){
    DEBUG((EFI_D_WARN, "PhyPowerWrapperInit failed!\n"));
    return;
  }
  DEBUG((EFI_D_WARN, "SS PHY init\n"));
  gBS->Stall(1000000);
  UINT8 a = MmioRead8(phy->reg_base + REG_BLOCK_OFFSET_USB_PHYSICAL_CODING_SUBLAYER + USB_PHYSICAL_CODING_SUBLAYER_STATUS);
  // Turn it off
  MmioWrite8(phy->reg_base + REG_BLOCK_OFFSET_COMMON + COMMON_POWER_DOWN_CTRL, 1);
  // Override HW control for reset of phy. Bits are for: DP PHY, DP PHY MUX, USB PHY, USB PHY MUX 
  MmioWrite8(phy->reg_base + REG_BLOCK_OFFSET_COMMON + COMMON_RESET_OVRD_CTRL, 0x0F);

  MmioWrite8(phy->reg_base + REG_BLOCK_OFFSET_COMMON + COMMON_TYPEC_CTRL, 1<<1 /*| 1<<0 (for inverted orientation) */);

  // Disable interrupts, we won't use them
  MmioWrite8(phy->reg_base + REG_BLOCK_OFFSET_COMMON + COMMON_SOFTWARE_INTERRUPT_CTRL, 0x03);
  // Make surre it's off
  gBS->Stall(10000000);

  // Turn it on
  MmioWrite8(phy->reg_base + REG_BLOCK_OFFSET_COMMON + COMMON_POWER_DOWN_CTRL, 1);
  // Undo HW reset override, else we can't change non-common-block registers
  MmioWrite8(phy->reg_base + REG_BLOCK_OFFSET_COMMON + COMMON_RESET_OVRD_CTRL, 0);

  MmioWrite8(phy->reg_base + REG_BLOCK_OFFSET_USB_PHYSICAL_CODING_SUBLAYER + USB_PHYSICAL_CODING_SUBLAYER_SW_RESET, 1); // in reset
  MmioWrite8(phy->reg_base + REG_BLOCK_OFFSET_USB_PHYSICAL_CODING_SUBLAYER + USB_PHYSICAL_CODING_SUBLAYER_POWER_DOWN_CONTROL, 1); // powered
  MmioWrite8(phy->reg_base + REG_BLOCK_OFFSET_USB_PHYSICAL_CODING_SUBLAYER + USB_PHYSICAL_CODING_SUBLAYER_START_CTRL, 0); // not started

  // Initialize registers
  for(int block_i=0; block_i<reg_block_count; block_i++){
    const UINTN block_base = phy->reg_base + reg_block_offset_list[block_i];
    for(const struct phy_init_entry *seq = init_sequence[block_i].sequence, *seq_end = init_sequence[block_i].sequence + init_sequence[block_i].sequence_count; seq<seq_end; seq++){
      MmioWrite8(block_base + seq->offset, seq->value);
    }
  }

  // TODO: enable pipe clock

  // Turn it on
  DEBUG((EFI_D_WARN, "SS PHY Start\n"));
  MmioWrite8(phy->reg_base + REG_BLOCK_OFFSET_USB_PHYSICAL_CODING_SUBLAYER + USB_PHYSICAL_CODING_SUBLAYER_POWER_DOWN_CONTROL, 0);
  MmioWrite8(phy->reg_base + REG_BLOCK_OFFSET_USB_PHYSICAL_CODING_SUBLAYER + USB_PHYSICAL_CODING_SUBLAYER_SW_RESET, 0);
  MmioWrite8(phy->reg_base + REG_BLOCK_OFFSET_USB_PHYSICAL_CODING_SUBLAYER + USB_PHYSICAL_CODING_SUBLAYER_START_CTRL, 0x03);

  UINT8 b = MmioRead8(phy->reg_base + REG_BLOCK_OFFSET_USB_PHYSICAL_CODING_SUBLAYER + USB_PHYSICAL_CODING_SUBLAYER_STATUS);
  gBS->Stall(1000000);
  UINT8 c = MmioRead8(phy->reg_base + REG_BLOCK_OFFSET_USB_PHYSICAL_CODING_SUBLAYER + USB_PHYSICAL_CODING_SUBLAYER_STATUS);

  DEBUG((EFI_D_WARN, "status: %02X %02X %02X\n", a,b,c));
  gBS->Stall(1000000);
}

void phy_init_test(void){
  struct QmpComboPhy phy = {
    .reg_base = 0x88e8000,
    .phy_power_config_index = 0,
    .lane_usage = SS_LU_USB,
  };
  phy_init(&phy);
}
