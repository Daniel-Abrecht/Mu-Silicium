

typedef struct npa_handle_v* npa_handle;

struct clock {
  const char* name;
  UINT32 frequency;
  UINT32 divider;
  UINTN id;
};

struct icb_arb {
  int type; // 0x04
  union {
    struct {
      UINT64 instantaneous_bandwidth;
      UINT64 arbitrated_bandwidth;
      UINT32 latency; // Latency requirement in nanoseconds
    } type04;
  };
};

struct npa_node {
  const char* resource;
  const char* client_name;
  npa_handle handle;
};

struct phy_power_wrapper {
  BOOLEAN init_done;
  struct npa_node npa_hs_phy;
  struct npa_node npa_ss_phy;
  struct npa_node npa_retimer;
  struct npa_node npa_icbarrb;
  const int (*bus_icbarb_relation)[2];
  const struct icb_arb* bus_icbarb_request;
  int bus_icbarb_count;
  const char* clock_power_domain;
  UINTN clock_power_domain_id;
  struct clock* clocks;
};

extern struct phy_power_wrapper*const usb_power_config[];
extern const UINTN usb_power_config_count;

extern EFI_STATUS PhyPowerWrapperInit(struct phy_power_wrapper* self);
extern EFI_STATUS PhyPowerWrapperPowerOn(struct phy_power_wrapper* self);
extern EFI_STATUS PhyPowerWrapperPowerOff(struct phy_power_wrapper* self);
