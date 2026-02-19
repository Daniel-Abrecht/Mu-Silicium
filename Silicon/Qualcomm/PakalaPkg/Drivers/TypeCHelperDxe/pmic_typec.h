

#define TYPEC_SNK_STATUS_REG   0x06
#define DETECTED_SNK_TYPE_MASK 0x7F
#define SNK_DAM_MASK           0x70
#define SNK_DAM_500MA          (1<<6)
#define SNK_DAM_1500MA         (1<<5)
#define SNK_DAM_3000MA         (1<<4)
#define SNK_RP_STD             (1<<3)
#define SNK_RP_1P5             (1<<2)
#define SNK_RP_3P0             (1<<1)
#define SNK_RP_SHORT           (1<<0)

#define TYPEC_DEBUG_ACCESS_STATUS_REG 0x07

#define TYPEC_SRC_STATUS_REG   0x08
#define DETECTED_SRC_TYPE_MASK 0x1F
#define SRC_HIGH_BATT          (1<<5)
#define SRC_DEBUG_ACCESS       (1<<4)
#define SRC_RD_OPEN            (1<<3)
#define SRC_RD_RA_VCONN        (1<<2)
#define SRC_RA_OPEN            (1<<1)
#define AUDIO_ACCESS_RA_RA     (1<<0)

#define TYPEC_STATE_MACHINE_STATUS_REG 0x09
#define TYPEC_ATTACH_DETACH_STATE      (1<<5)

#define TYPEC_SM_STATUS_REG   0x0A
#define TYPEC_SM_VBUS_VSAFE5V (1<<5)
#define TYPEC_SM_VBUS_VSAFE0V (1<<6)
#define TYPEC_SM_USBIN_LT_LV  (1<<7)

#define TYPEC_MISC_STATUS_REG        0x0B
#define TYPEC_WATER_DETECTION_STATUS (1<<7)
#define SNK_SRC_MODE                 (1<<6)
#define TYPEC_VBUS_DETECT            (1<<5)
#define TYPEC_VBUS_ERROR_STATUS      (1<<4)
#define TYPEC_DEBOUNCE_DONE          (1<<3)
#define CC_ORIENTATION               (1<<1)
#define CC_ATTACHED                  (1<<0)

#define LEGACY_CABLE_STATUS_REG           0x0D
#define TYPEC_LEGACY_CABLE_STATUS         (1<<1)
#define TYPEC_NONCOMP_LEGACY_CABLE_STATUS (1<<0)

#define TYPEC_U_USB_STATUS_REG 0x0F
#define U_USB_GROUND_NOVBUS    (1<<6)
#define U_USB_GROUND           (1<<4)
#define U_USB_FMB1             (1<<3)
#define U_USB_FLOAT1           (1<<2)
#define U_USB_FMB2             (1<<1)
#define U_USB_FLOAT2           (1<<0)

#define TYPEC_MODE_CFG_REG        0x44
#define TYPEC_TRY_MODE_MASK       0x18
#define EN_TRY_SNK                (1<<4)
#define EN_TRY_SRC                (1<<3)
#define TYPEC_POWER_ROLE_CMD_MASK 0x07
#define EN_SRC_ONLY               (1<<2)
#define EN_SNK_ONLY               (1<<1)
#define TYPEC_DISABLE_CMD         (1<<0)

#define TYPEC_VCONN_CONTROL_REG 0x46
#define VCONN_EN_ORIENTATION    (1<<2)
#define VCONN_EN_VALUE          (1<<1)
#define VCONN_EN_SRC            (1<<0)

#define TYPEC_CCOUT_CONTROL_REG 0x48
#define TYPEC_CCOUT_BUFFER_EN   (1<<2)
#define TYPEC_CCOUT_VALUE       (1<<1)
#define TYPEC_CCOUT_SRC         (1<<0)

#define DEBUG_ACCESS_SNK_CFG_REG 0x4A

#define DEBUG_ACCESS_SRC_CFG_REG       0x4C
#define EN_UNORIENTED_DEBUG_ACCESS_SRC (1<<0)

#define TYPE_C_CRUDE_SENSOR_CFG_REG 0x4e
#define EN_SRC_CRUDE_SENSOR (1<<1)
#define EN_SNK_CRUDE_SENSOR (1<<0)

#define TYPEC_EXIT_STATE_CFG_REG        0x50
#define BYPASS_VSAFE0V_DURING_ROLE_SWAP (1<<3)
#define SEL_SRC_UPPER_REF               (1<<2)
#define USE_TPD_FOR_EXITING_ATTACHSRC   (1<<1)
#define EXIT_SNK_BASED_ON_CC            (1<<0)

#define TYPEC_CURRSRC_CFG_REG  0x52
#define TYPEC_SRC_RP_SEL_330UA (1<<1)
#define TYPEC_SRC_RP_SEL_180UA (1<<0)
#define TYPEC_SRC_RP_SEL_80UA  0
#define TYPEC_SRC_RP_SEL_MASK  0x03

#define TYPEC_INTERRUPT_EN_CFG_1_REG 0x5E
#define TYPEC_LEGACY_CABLE_INT_EN (1<<7)
#define TYPEC_NONCOMPLIANT_LEGACY_CABLE_INT_EN (1<<6)
#define TYPEC_TRYSOURCE_DETECT_INT_EN (1<<5)
#define TYPEC_TRYSINK_DETECT_INT_EN (1<<4)
#define TYPEC_CCOUT_DETACH_INT_EN (1<<3)
#define TYPEC_CCOUT_ATTACH_INT_EN (1<<2)
#define TYPEC_VBUS_DEASSERT_INT_EN (1<<1)
#define TYPEC_VBUS_ASSERT_INT_EN (1<<0)

#define TYPEC_INTERRUPT_EN_CFG_2_REG      0x60
#define TYPEC_SRC_BATT_HPWR_INT_EN        (1<<6)
#define MICRO_USB_STATE_CHANGE_INT_EN     (1<<5)
#define TYPEC_STATE_MACHINE_CHANGE_INT_EN (1<<4)
#define TYPEC_DEBUG_ACCESS_DETECT_INT_EN  (1<<3)
#define TYPEC_WATER_DETECTION_INT_EN      (1<<2)
#define TYPEC_VBUS_ERROR_INT_EN           (1<<1)
#define TYPEC_DEBOUNCE_DONE_INT_EN        (1<<0)

#define TYPEC_DEBOUNCE_OPTION_REG 0x62
#define REDUCE_TCCDEBOUNCE_TO_2MS (1<<2)

#define TYPE_C_SBU_CFG_REG 0x6A
#define SEL_SBU1_ISRC_VAL  0x04
#define SEL_SBU2_ISRC_VAL  0x01

#define TYPEC_U_USB_CFG_REG       0x70
#define EN_MICRO_USB_FACTORY_MODE (1<<1)
#define EN_MICRO_USB_MODE         (1<<0)

#define TYPEC_PMI632_U_USB_WATER_PROTECTION_CFG_REG 0x72

#define TYPEC_U_USB_WATER_PROTECTION_CFG_REG 0x73
#define EN_MICRO_USB_WATER_PROTECTION        (1<<4)
#define MICRO_USB_DETECTION_ON_TIME_CFG_MASK 0x0C
#define MICRO_USB_DETECTION_PERIOD_CFG_MASK  0x03

#define TYPEC_PMI632_MICRO_USB_MODE_REG 0x73
#define MICRO_USB_MODE_ONLY             (1<<0)

/* Interrupt numbers */
#define PMIC_TYPEC_OR_RID_IRQ        0x0
#define PMIC_TYPEC_VPD_IRQ           0x1
#define PMIC_TYPEC_CC_STATE_IRQ      0x2
#define PMIC_TYPEC_VCONN_OC_IRQ      0x3
#define PMIC_TYPEC_VBUS_IRQ          0x4
#define PMIC_TYPEC_ATTACH_DETACH_IRQ 0x5
#define PMIC_TYPEC_LEGACY_CABLE_IRQ  0x6
#define PMIC_TYPEC_TRY_SNK_SRC_IRQ   0x7
