
typedef EFI_STATUS (EFIAPI *GPIO_ENABLE)(UINT32 pmic, int gpio, BOOLEAN enable);
typedef EFI_STATUS (EFIAPI *GPIO_CFG_MODE)(UINT32 pmic, int gpio, int type);
typedef EFI_STATUS (EFIAPI *GPIO_INPUT_LEVEL_STATUS)(UINT32 pmic, int gpio, int *ret);
typedef EFI_STATUS (EFIAPI *GPIO_IRQ_ENABLE)(UINT32 pmic, int gpio, BOOLEAN enable);
typedef EFI_STATUS (EFIAPI *GPIO_IRQ_CLEAR)(UINT32 pmic, int gpio);
typedef EFI_STATUS (EFIAPI *GPIO_IRQ_SET_TRIGGER)(UINT32 pmic, int gpio, int trigger);
typedef EFI_STATUS (EFIAPI *GPIO_IRQ_STATUS)(UINT32 pmic, int gpio, int type, BOOLEAN* status);
typedef EFI_STATUS (EFIAPI *GPIO_SET_VOLTAGE_SOURCE)(UINT32 pmic, int gpio, int Voltage_Source);
typedef EFI_STATUS (EFIAPI *GPIO_SET_OUT_BUF_CFG)(UINT32 pmic, int gpio, int config);
typedef EFI_STATUS (EFIAPI *GPIO_SET_OUTPUT_LEVEL)(UINT32 pmic, int gpio, int level);
typedef EFI_STATUS (EFIAPI *GPIO_SET_OUT_DRV_STR)(UINT32 pmic, int gpio, int strength);
typedef EFI_STATUS (EFIAPI *GPIO_SET_OUT_SRC_CFG)(UINT32 pmic, int gpio, int source);


typedef struct _EFI_QCOM_PMIC_GPIO_PROTOCOL {
  UINT64                  Revision;
  GPIO_ENABLE             Enable;
  GPIO_CFG_MODE           CfgMode;
  GPIO_INPUT_LEVEL_STATUS InputLevelStatus;
  GPIO_IRQ_ENABLE         IrqEnable;
  GPIO_IRQ_CLEAR          IrqClear;
  GPIO_IRQ_SET_TRIGGER    IrqSetTrigger;
  GPIO_IRQ_STATUS         IrqStatus;
  GPIO_SET_VOLTAGE_SOURCE SetVoltageSource;
  GPIO_SET_OUT_BUF_CFG    SetOutBufCfg;
  GPIO_SET_OUTPUT_LEVEL   SetOutputLevel;
  GPIO_SET_OUT_DRV_STR    SetOutDrvStr;
  GPIO_SET_OUT_SRC_CFG    SetOutSrcCfg;
  // GPIO_STATUS_GET         StatusGet;
} EFI_QCOM_PMIC_GPIO_PROTOCOL;
