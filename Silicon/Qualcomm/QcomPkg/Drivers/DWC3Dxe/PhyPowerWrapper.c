#include "common.h"
#include "phy_power.h"
#include <Library/IoLib.h>
#include <Protocol/EFIClock.h>

extern EFI_GUID gEfiClockProtocolGuid;
extern EFI_GUID gEfiNpaProtocolGuid;

struct phy_power_wrapper usb0_power_config = {
  .npa_hs_phy = {
    .resource = "/pmic/client/usb_hs0",
    .client_name = "usb_hs0",
  },
  .npa_ss_phy = {
    .resource = "/pmic/client/usb_ss0",
    .client_name = "usb_ss0",
  },
  // .npa_retimer = {
  //   .resource = "/pmic/client/usb_ss0_retimer",
  //   .client_name = "usb_ss0_retimer",
  // },
  .npa_icbarrb = {
    .resource = "/icb/arbiter",
    .client_name = "usb_ss0_bus",
  },
  .bus_icbarb_relation = (const int[2][2]){
    {0,22}, {32,0} // APPSS_PROC -> USB3_0, USB3_0 -> EBI1
  },
  .bus_icbarb_request = (struct icb_arb[]){
    {
      .type = 0x04,
      .type04.instantaneous_bandwidth = 400000000,
    },{
      .type = 0x04,
      .type04.instantaneous_bandwidth = 670000000,
      .type04.arbitrated_bandwidth = 670000000,
    },
  },
  .bus_icbarb_count = 2,
  .clock_power_domain = "gcc_usb30_prim_gdsc",
  .clocks = (struct clock[]){
    {"gcc_cfg_noc_usb3_prim_axi_clk", 200000000, 1 },
    {"gcc_aggre_usb3_prim_axi_clk",   200000000, 1 },
    {"gcc_usb30_prim_master_clk",     200000000, 1 },
    {"gcc_usb30_prim_sleep_clk",              0, 1 },
    {"gcc_usb30_prim_mock_utmi_clk",   19200000, 1 },
    {"gcc_usb3_prim_phy_aux_clk",      19200000, 1 },
    {"gcc_usb3_prim_phy_com_aux_clk",  19200000, 1 },
    {"gcc_usb3_prim_phy_pipe_clk",            0, 1 },

    {"gcc_usb3_phy_prim_bcr", 0, 0 },
    {"gcc_usb3_dp_phy_prim_bcr", 0, 0 },
    {0}
  },
};
struct phy_power_wrapper*const usb_power_config[] = {
  &usb0_power_config
};
const UINTN usb_power_config_count = sizeof(usb_power_config)/sizeof(*usb_power_config);

enum npa_power {
  NPA_OFF,
  NPA_STANDBY,
  NPA_ACTIVE,
};

enum npa_client_type {
  NPA_CLIENT_REQUIRED = 1<<6,
  NPA_CLIENT_VECTOR = 1<<10,
};


struct _EFI_NPA_PROTOCOL {
  UINT64 Revision;
  void* _1;
  EFI_STATUS (EFIAPI* CreateSyncClientEx)(const char* resource, const char* name, enum npa_client_type type, unsigned int data_size, const void* data, npa_handle *ret_handle);
  void* _3;
  void* _4;
  EFI_STATUS (EFIAPI* ScalarRequest)(npa_handle client, unsigned int state);
  void* _6;
  void* _7;
  void* _8;
  void* _9;
  void* _10;
  void* _11;
  void* _12;
  void* _13;
  void* _14;
  void* _15;
  void* _16;
  void* _17;
  EFI_STATUS (EFIAPI* VectorRequest)(npa_handle client, unsigned int count, const void* value); // Note: make sure to divide count by sizeof(int)
};
typedef struct _EFI_NPA_PROTOCOL EFI_NPA_PROTOCOL;

EFI_NPA_PROTOCOL* NPAProtocol;
EFI_CLOCK_PROTOCOL* ClockProtocol;

/////////////// A special clock to be enabled using TLMM, and some signal routing
#define TLMM_BASE 0x0f000000
#define TLMM_REG_OFFSET 0x00100000
#define TLMM_REG_PHY2_QREF_OFFSET 0x106008
#define TLMM_REG_PHY3_QREF_OFFSET 0x107008
#define TLMM_REG_PHY2_QREF_ELEM_TX_RPT_SEL_OFFSET 0x106000
#define TLMM_REG_PHY3_QREF_ELEM_TX_RPT_SEL_OFFSET 0x107000
#define TLMM_REG_QREF_PHY_SEL_0_OFFSET 0x103000

#define TLMM_REG_PHY2_QREF \
  TLMM_BASE + TLMM_REG_OFFSET + TLMM_REG_PHY2_QREF_OFFSET
#define TLMM_REG_PHY3_QREF \
  TLMM_BASE + TLMM_REG_OFFSET + TLMM_REG_PHY3_QREF_OFFSET
#define TLMM_PHY2_QREF_ELEM_TX_RPT_SEL \
  TLMM_BASE + TLMM_REG_OFFSET + TLMM_REG_PHY2_QREF_ELEM_TX_RPT_SEL_OFFSET
#define TLMM_PHY3_QREF_ELEM_TX_RPT_SEL \
  TLMM_BASE + TLMM_REG_OFFSET + TLMM_REG_PHY3_QREF_ELEM_TX_RPT_SEL_OFFSET
#define TLMM_QREF_PHY_SEL_0 \
  TLMM_BASE + TLMM_REG_OFFSET + TLMM_REG_QREF_PHY_SEL_0_OFFSET
//////////////////////////


EFI_STATUS PhyPowerWrapperInit(struct phy_power_wrapper* self){
  if(self->init_done)
    return EFI_SUCCESS;
  EFI_STATUS Status, All=0;
  if(!NPAProtocol){
    Status = gBS->LocateProtocol(&gEfiNpaProtocolGuid, NULL, (void**)&NPAProtocol);
    if(EFI_ERROR(Status)){
      DEBUG((EFI_D_ERROR, "Failed to Locate EFI_NPA_PROTOCOL Protocol! Status = %r\n", Status));
      return Status;
    }
  }
  if(!ClockProtocol){
    Status = gBS->LocateProtocol(&gEfiClockProtocolGuid, NULL, (void**)&ClockProtocol);
    if(EFI_ERROR(Status)){
      DEBUG((EFI_D_ERROR, "Failed to Locate EFI_NPA_PROTOCOL Protocol! Status = %r\n", Status));
      return Status;
    }
  }
  All |= Status = NPAProtocol->CreateSyncClientEx(self->npa_hs_phy.resource, self->npa_hs_phy.client_name, NPA_CLIENT_REQUIRED, 0, NULL, &self->npa_hs_phy.handle);
  if(EFI_ERROR(Status)) DEBUG((EFI_D_ERROR, "CreateSyncClientEx for npa_hs_phy failed: %r\n", Status));
  All |= Status = NPAProtocol->CreateSyncClientEx(self->npa_ss_phy.resource, self->npa_ss_phy.client_name, NPA_CLIENT_REQUIRED, 0, NULL, &self->npa_ss_phy.handle);
  if(EFI_ERROR(Status)) DEBUG((EFI_D_ERROR, "CreateSyncClientEx for npa_ss_phy failed: %r\n", Status));
  if(self->npa_retimer.resource){
    All |= Status = NPAProtocol->CreateSyncClientEx(self->npa_retimer.resource, self->npa_retimer.client_name, NPA_CLIENT_REQUIRED, 0, NULL, &self->npa_retimer.handle);
    if(EFI_ERROR(Status)) DEBUG((EFI_D_ERROR, "CreateSyncClientEx for npa_retimer failed: %r\n", Status));
  }
  All |= Status = NPAProtocol->CreateSyncClientEx(self->npa_icbarrb.resource, self->npa_icbarrb.client_name, NPA_CLIENT_VECTOR, sizeof(*self->bus_icbarb_relation)*self->bus_icbarb_count, self->bus_icbarb_relation, &self->npa_icbarrb.handle);
  if(EFI_ERROR(Status)) DEBUG((EFI_D_ERROR, "CreateSyncClientEx for npa_icbarrb failed: %r\n", Status));
  All |= Status = ClockProtocol->GetClockPowerDomainID(ClockProtocol, self->clock_power_domain, &self->clock_power_domain_id);
  if(EFI_ERROR(Status)) DEBUG((EFI_D_ERROR, "GetClockPowerDomainID failed: %r\n", Status));

  for(struct clock* it=self->clocks; it->name; it++){
    All |= Status = ClockProtocol->GetClockID(ClockProtocol, it->name, &it->id);
    if(EFI_ERROR(Status)) DEBUG((EFI_D_ERROR, "GetClockID failed: %r\n", Status));
  }

  //// TLMM: configure some signal routing stuff ////
  MmioWrite32(TLMM_PHY2_QREF_ELEM_TX_RPT_SEL, 0x307F);
  MmioWrite32(TLMM_PHY3_QREF_ELEM_TX_RPT_SEL, 0x307F);

  UINT32 phy_select = MmioRead32(TLMM_QREF_PHY_SEL_0);
  phy_select = (phy_select & ~0xFFFF) | 0x000C;
  MmioWrite32(TLMM_QREF_PHY_SEL_0, phy_select);
  ///////////////////////////////////////////////////

  self->init_done = TRUE;
  return All;
}

EFI_STATUS PhyPowerWrapperPowerOn(struct phy_power_wrapper* self){
  EFI_STATUS Status, All=0;
  All |= Status =  ClockProtocol->DisableClockPowerDomain(ClockProtocol, self->clock_power_domain_id);
  if(EFI_ERROR(Status)) DEBUG((EFI_D_ERROR, "EnableClockPowerDomain failed: %r\n", Status));
  gBS->Stall (100);
  All |= Status =  ClockProtocol->EnableClockPowerDomain(ClockProtocol, self->clock_power_domain_id);
  if(EFI_ERROR(Status)) DEBUG((EFI_D_ERROR, "EnableClockPowerDomain failed: %r\n", Status));
  gBS->Stall (100);
  All |= Status = NPAProtocol->ScalarRequest(self->npa_hs_phy.handle, NPA_ACTIVE);
  if(EFI_ERROR(Status)) DEBUG((EFI_D_ERROR, "Request for npa_hs_phy ACTIVE failed: %r\n", Status));
  All |= Status = NPAProtocol->ScalarRequest(self->npa_ss_phy.handle, NPA_ACTIVE);
  if(EFI_ERROR(Status)) DEBUG((EFI_D_ERROR, "Request for npa_ss_phy ACTIVE failed: %r\n", Status));
  if(self->npa_retimer.resource){
    All |= Status = NPAProtocol->ScalarRequest(self->npa_retimer.handle, NPA_ACTIVE);
    if(EFI_ERROR(Status)) DEBUG((EFI_D_ERROR, "Request for npa_retimer ACTIVE failed: %r\n", Status));
  }
  All |= Status = NPAProtocol->VectorRequest(self->npa_icbarrb.handle, sizeof(*self->bus_icbarb_request)*self->bus_icbarb_count/sizeof(int), self->bus_icbarb_request);
  if(EFI_ERROR(Status)) DEBUG((EFI_D_ERROR, "Request for npa_icbarrb failed: %r\n", Status));
  gBS->Stall (1000000);
  //// Enable TLMM phy clocks ////
  MmioWrite32(TLMM_REG_PHY2_QREF, MmioRead32(TLMM_REG_PHY2_QREF) | 1);
  MmioWrite32(TLMM_REG_PHY3_QREF, MmioRead32(TLMM_REG_PHY2_QREF) | 1);
  ////////////////////////////////
  for(const struct clock* it=self->clocks; it->name; it++){
    All |= Status =  ClockProtocol->DisableClock(ClockProtocol, it->id);
    if(EFI_ERROR(Status)) DEBUG((EFI_D_ERROR, "DisableClock(%a) failed: %r\n", it->name, Status));
  }
  // for(const struct clock* it=self->clocks; it->name; it++){
  //   All |= Status =  ClockProtocol->ResetClock(ClockProtocol, it->id, EFI_CLOCK_RESET_ASSERT);
  //   if(EFI_ERROR(Status)) DEBUG((EFI_D_ERROR, "ResetClock(%a, ASSERT) failed: %r\n", it->name, Status));
  // }
  gBS->Stall (1000000);
  for(const struct clock* it=self->clocks; it->name; it++){
    if(!it->id) continue;
    UINT32 ret_frequency = 0;
    Status = 0;
    if(it->frequency)
      Status |= ClockProtocol->SetClockFreqHz(ClockProtocol, it->id, it->frequency, 0, &ret_frequency);
    if(it->divider)
      Status |= ClockProtocol->SetClockDivider(ClockProtocol, it->id, it->divider);
    if(EFI_ERROR(Status)){
      DEBUG((EFI_D_ERROR, "Failed to set clock %a frequency %d/%d: %r\n", it->name, it->frequency, it->divider, Status));
      continue;
    }
    DEBUG((EFI_D_WARN, "Clock %a set frequency %d/%d ret_frequency: %d\n", it->name, it->frequency, it->divider, ret_frequency));
    All |= Status =  ClockProtocol->EnableClock(ClockProtocol, it->id);
    if(EFI_ERROR(Status)) DEBUG((EFI_D_ERROR, "EnableClock(%a) failed: %r\n", it->name, Status));
    gBS->Stall (100000);
  }
  gBS->Stall (1000000);
  for(const struct clock* it=self->clocks; it->name; it++){
    All |= Status =  ClockProtocol->ResetClock(ClockProtocol, it->id, EFI_CLOCK_RESET_DEASSERT);
    if(EFI_ERROR(Status)) DEBUG((EFI_D_ERROR, "ResetClock(%a, DEASSERT) failed: %r\n", it->name, Status));
  }
  gBS->Stall (1000000);
  return All;
}

EFI_STATUS PhyPowerWrapperPowerOff(struct phy_power_wrapper* self){
  EFI_STATUS Status, All=0;
  for(const struct clock* it=self->clocks; it->name; it++){
    All |= Status =  ClockProtocol->DisableClock(ClockProtocol, it->id);
    if(EFI_ERROR(Status)) DEBUG((EFI_D_ERROR, "DisableClock(%a) failed: %r\n", it->name, Status));
  }
  //// Disable TLMM phy clocks ////
  MmioWrite32(TLMM_REG_PHY2_QREF, MmioRead32(TLMM_REG_PHY2_QREF) & ~1);
  MmioWrite32(TLMM_REG_PHY3_QREF, MmioRead32(TLMM_REG_PHY2_QREF) & ~1);
  ////////////////////////////////
  All |= Status =  ClockProtocol->DisableClockPowerDomain(ClockProtocol, self->clock_power_domain_id);
  if(EFI_ERROR(Status)) DEBUG((EFI_D_ERROR, "DisableClockPowerDomain failed: %r\n", Status));
  {
    struct icb_arb icb[self->bus_icbarb_count];
    gBS->SetMem(&icb, sizeof(icb), 0);
    for(int i=0; i<self->bus_icbarb_count; i++)
      icb[i].type = self->bus_icbarb_request[i].type;
    All |= Status = NPAProtocol->VectorRequest(self->npa_icbarrb.handle, sizeof(*self->bus_icbarb_request)*self->bus_icbarb_count/sizeof(int), self->bus_icbarb_request);
    if(EFI_ERROR(Status)) DEBUG((EFI_D_ERROR, "Request for npa_icbarrb failed: %r\n", Status));
  }
  if(self->npa_retimer.resource){
    All |= Status = NPAProtocol->ScalarRequest(self->npa_retimer.handle, NPA_STANDBY);
    if(EFI_ERROR(Status)) DEBUG((EFI_D_ERROR, "Request for npa_retimer STANDBY failed: %r\n", Status));
  }
  All |= Status = NPAProtocol->ScalarRequest(self->npa_ss_phy.handle, NPA_STANDBY);
  if(EFI_ERROR(Status)) DEBUG((EFI_D_ERROR, "Request for npa_ss_phy STANDBY failed: %r\n", Status));
  All |= Status = NPAProtocol->ScalarRequest(self->npa_hs_phy.handle, NPA_STANDBY);
  if(EFI_ERROR(Status)) DEBUG((EFI_D_ERROR, "Request for npa_hs_phy STANDBY failed: %r\n", Status));
  return All;
}
