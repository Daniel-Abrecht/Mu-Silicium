#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>

/**
 * WARNING: With great power comes great responsibiliy!
 * This controls voltage regulators, controlling what gets how much power, many volts and amps.
 * Give too much power to the wrong thing, an it may go up in smoke.
 */
/*
void rpmh_vrm_regulator_set(){
  
}

struct RpmhRegulatorMode {
  enum rpmh_regulator_mode mode;
  unsigned threshold_current;
};

struct RpmhVrmRegulator {
  const char* resource_name;
  enum rpmh_regulator_type regulator_type;
  unsigned regulator_mode_count;
  struct RpmhRegulatorMode* regulator_mode;
  
};
*/
/*
rpmh-regulator-ldob9 {
  compatible = "qcom,rpmh-vrm-regulator";
  qcom,resource-name = "ldob9";
  qcom,regulator-type = "pmic5-ldo";
  qcom,supported-modes = <RPMH_REGULATOR_MODE_LPM RPMH_REGULATOR_MODE_HPM>; // <0x02 0x04>;
  qcom,mode-threshold-currents = <0 10000>;

  L9B: pm_humu_l9: regulator-pm-humu-l9 {
      regulator-name = "pm_humu_l9";
      qcom,set = <RPMH_REGULATOR_SET_ALL>; // <0x03>;
      regulator-min-microvolt = <2960000>;
      regulator-max-microvolt = <3104000>;
      qcom,init-voltage = <3104000>;
      qcom,init-mode = <RPMH_REGULATOR_MODE_HPM>; // <0x04>;
  };
};
*/
