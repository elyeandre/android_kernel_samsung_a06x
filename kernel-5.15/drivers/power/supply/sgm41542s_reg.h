#ifndef __SGM41542__HEADER__
#define __SGM41542__HEADER__

/* define register */
#define SGM41542S_INPUT_CURR_LIMIT	0x00
#define SGM41542S_CHRG_CTRL_1	    0x01
#define SGM41542S_CHRG_CURR_LIMIT	0x02
#define SGM41542S_CHRG_PRE_TERM	    0x03
#define SGM41542S_BAT_VOLT_LIMIT	0x04
#define SGM41542S_CHRG_CTRL_2	    0x05
#define SGM41542S_CHRG_CTRL_3	    0x06
#define SGM41542S_CHRG_CTRL_4	    0x07
#define SGM41542S_CHRG_STAT_1	    0x08
#define SGM41542S_CHRG_FAULT	    0x09
#define SGM41542S_CHRG_STAT_2	    0x0a
#define SGM41542S_PART_INFO	        0x0b
#define SGM41542S_CHRG_CTRL_5	    0x0c
#define SGM41542S_CHRG_CTRL_6	    0x0d
#define SGM41542S_CHRG_FLAG	        0x0e
#define SGM41542S_CHRG_CTRL_7	    0x0f
#define SGM41542S_CHRG_CTRL_8	    0x10
#define SGM41542S_CHRG_CTRL_9	    0x11
#define SGM41542S_CHRG_CTRL_10	    0x12
#define SGM41542S_CHRG_VBAT_ADC	    0x13
#define SGM41542S_CHRG_VSYS_ADC	    0x14
#define SGM41542S_CHRG_TS_ADC	    0x15
#define SGM41542S_CHRG_VBUS_ADC	    0x16
#define SGM41542S_CHRG_IBAT_ADC	    0x17
#define SGM41542S_CHRG_IBUS_ADC	    0x18
#define SGM41542S_ADC_FUNC_CTRL	    0x19
#define SGM41542S_IR_COMPENSATION	0x1a
#define SGM41542S_TFCP_CTRL_1	    0x1b
#define SGM41542S_TFCP_CTRL_2	    0x1c

//reg 00
#define SGM41542S_EN_ICHG_MON         GENMASK(7, 6)
/* iindpm current */
#define SGM41542S_IINDPM_I_MASK		  GENMASK(5, 0)
#define SGM41542S_IINDPM_I_MIN_uA     100000
#define SGM41542S_IINDPM_I_MAX_uA	  3300000
#define SGM41542S_IINDPM_STEP_uA	  100000
#define SGM41542S_IINDPM_DEF_uA		  2400000

//reg 01
#define SGM41542S_PFM_DIS			BIT(7)
#define SGM41542S_WD_RST			BIT(6)
#define SGM41542S_OTG_EN			BIT(5)
#define SGM41542S_CHRG_EN		    BIT(4)
#define SGM41542S_SYS_MIN_MASK		GENMASK(3, 1)
#define SGM41542S_MIN_BAT_SEL		BIT(0)

//reg 02
#define SGM41542S_Q1_FULLON		    BIT(7)
/* charge current */
#define SGM41542S_ICHRG_CUR_MASK	GENMASK(6, 0)
#define SGM41542S_ICHRG_I_STEP_uA	  60000
#define SGM41542S_ICHRG_I_MIN_uA		0
#define SGM41542S_ICHRG_I_MAX_uA		5100000
#define SGM41542S_ICHRG_I_DEF_uA		2040000

//reg 03
/* precharge current */
#define SGM41542S_PRECHRG_CUR_MASK		GENMASK(7, 4)
#define SGM41542S_PRECHRG_CURR_STEP_uA	60000
#define SGM41542S_PRECHRG_I_MIN_uA		60000
#define SGM41542S_PRECHRG_I_MAX_uA		780000
#define SGM41542S_PRECHRG_I_DEF_uA		180000

/* termination current */
#define SGM41542S_TERMCHRG_CUR_MASK		GENMASK(3, 0)
#define SGM41542S_TERMCHRG_CURR_STEP_uA	60000
#define SGM41542S_TERMCHRG_I_MIN_uA		60000
#define SGM41542S_TERMCHRG_I_MAX_uA		960000
#define SGM41542S_TERMCHRG_I_DEF_uA		180000

//reg 04
/* charge voltage */
#define SGM41542S_VREG_V_MASK		GENMASK(7, 1)
#define SGM41542S_VREG_V_SHIFT		1
#define SGM41542S_VREG_V_MAX_uV		4770000
#define SGM41542S_VREG_V_MIN_uV		3500000
#define SGM41542S_VREG_V_DEF_uV		4435000
#define SGM41542S_VREG_V_STEP_uV	10000

//reg 05
#define SGM41542S_EN_TERM		  BIT(7)
#define SGM41542S_EN_TSD_DISFET	  BIT(6)
#define SGM41542S_WATCHDOG_MASK	  GENMASK(5, 4)
#define SGM41542S_EN_TIMER        BIT(3)
#define SGM41542S_CHG_TIMER       BIT(2)
#define SGM41542S_TREG            BIT(1)
#define SGM41542S_JEITA_ISET_L    BIT(0)

//reg 06
#define SGM41542S_EN_ILIM		  BIT(7)
#define SGM41542S_BOOSTV		  GENMASK(6, 4)
#define SGM41542S_BOOSTV_SHIFT	  4
/* vindpm voltage */
#define SGM41542S_VINDPM_V_MASK		GENMASK(3, 0)
#define SGM41542S_VINDPM_V_MIN_uV	3900000
#define SGM41542S_VINDPM_V_MAX_uV	12000000
#define SGM41542S_VINDPM_STEP_uV	100000
#define SGM41542S_VINDPM_DEF_uV		4500000
#define SGM41542S_VINDPM_OS_MASK	GENMASK(1, 0)

//reg 07
#define SGM41542S_IINDET_EN		  BIT(7)
#define SGM41542S_TMR2X_EN		  BIT(6)
#define SGM41542S_BATFET_DIS	  BIT(5)
#define SGM41542S_JEITA_VSET_H	  BIT(4)
#define SGM41542S_BATFET_DLY	  BIT(3)
#define SGM41542S_BATFET_RST_EN	  BIT(2)
#define SGM41542S_VDPM_BAT_TRACK  GENMASK(1, 0)

//reg 08
/* charge type */
#define SGM41542S_VBUS_STAT_MASK		GENMASK(7, 5)
#define SGM41542S_VBUS_STAT_SHIFT		5
#define SGM41542S_NOT_INPUT		0
#define SGM41542S_USB_SDP		BIT(5)
#define SGM41542S_USB_CDP		BIT(6)
#define SGM41542S_USB_DCP		(BIT(5) | BIT(6))
#define SGM41542S_UNKNOWN		(BIT(7) | BIT(5))
#define SGM41542S_NON_STANDARD	(BIT(7) | BIT(6))
#define SGM41542S_OTG_MODE		(BIT(7) | BIT(6) | BIT(5))

/* charge status */
#define SGM41542S_CHG_STAT_MASK		GENMASK(4, 3)
#define SGM41542S_CHG_STAT_SHIFT		3
#define SGM41542S_NOT_CHRGING	    0
#define SGM41542S_PRECHRG		    BIT(3)
#define SGM41542S_FAST_CHRG		    BIT(4)
#define SGM41542S_TERM_CHRG		    (BIT(3) | BIT(4))

#define SGM41542S_PG_STAT		    BIT(2)
#define SGM41542S_THERM_STAT		BIT(1)
#define SGM41542S_VSYS_STAT		    BIT(0)


//reg 09
//Fault status
#define SGM41542S_WDT_FAULT_MASK		BIT(7)
#define SGM41542S_BOOST_FAULT_MASK		BIT(6)
#define SGM41542S_CHRG_FAULT_MASK		GENMASK(5, 4)
#define SGM41542S_CHRG_FAULT_SHIFT		4
#define SGM41542S_BAT_FAULT_MASK		BIT(3)
/* TEMP Status */
#define SGM41542S_TEMP_MASK			GENMASK(2, 0)
#define SGM41542S_TEMP_SHIFT		0
#define SGM41542S_TEMP_NORMAL		BIT(0)
#define SGM41542S_TEMP_WARM			BIT(1)
#define SGM41542S_TEMP_COOL			(BIT(0) | BIT(1))
#define SGM41542S_TEMP_COLD			(BIT(0) | BIT(3))
#define SGM41542S_TEMP_HOT			(BIT(2) | BIT(3))

//reg 0A
//Fault status
#define SGM41542S_VBUS_GD			BIT(7)
#define SGM41542S_VINDPM_STAT		BIT(6)
#define SGM41542S_IINDPM_STAT		BIT(5)
#define SGM41542S_CV_STAT		    BIT(4)
#define SGM41542S_TOPOFF_ACTIVE		BIT(3)
#define SGM41542S_ACOV_STAT		    BIT(2)
#define SGM41542S_VINDPM_INT_MASK	BIT(1)
#define SGM41542S_IINDPM_INT_MASK	BIT(0)

//reg 0B
#define SGM41542S_REG_RST			BIT(7)
#define SGM41542S_PN_MASK		    GENMASK(6, 3)
#define SGM41542S_PN		        (BIT(6) | BIT(4))

//reg 0C
#define SGM41542S_JEITA_VSET_L   	BIT(7)
#define SGM41542S_JEITA_ISET_L_EN	BIT(6)
#define SGM41542S_JEITA_ISET_H   	GENMASK(5, 4)
#define SGM41542S_JEITA_VT2      	GENMASK(3, 2)
#define SGM41542S_JEITA_VT3      	GENMASK(1, 0)

//reg 0D
#define SGM41542S_EN_PUMPX		BIT(7)
#define SGM41542S_PUMPX_UP		BIT(6)
#define SGM41542S_PUMPX_DN		BIT(5)
#define SGM41542S_JEITA_SET		BIT(0)

//reg 0E
#define SGM41542S_INPUT_DET_DONE		BIT(7)

//reg 0F
#define SGM41542S_tSHIPMODE_MASK		BIT(5)
#define SGM41542S_ISHORT_SET_MASK		BIT(4)
#define SGM41542S_STAT_SET_MASK		    GENMASK(3, 2)
#define SGM41542S_VINDPM_OS_MASK		GENMASK(1, 0)

//reg 10
#define SGM41542S_VAC_OVP_MASK		GENMASK(7, 6)
#define SGM41542S_ITERM_TIMER		BIT(5)
#define SGM41542S_ITERM_SEL		    BIT(4)
#define SGM41542S_IPRECHG_SEL		BIT(3)
#define SGM41542S_BOOST_LIM			GENMASK(2, 0)

//reg 11
#define SGM41542S_BATLOWV		    GENMASK(7, 6)
#define SGM41542S_CONV_START		BIT(5)
#define SGM41542S_CONV_RATE		    BIT(4)
#define SGM41542S_TOPOFF_TIMER	    GENMASK(3, 2)
#define SGM41542S_VRECHARGE_MASK	GENMASK(1, 0)


//reg 12
#define SGM41542S_IBATOCP		    GENMASK(7, 6)
#define SGM41542S_IBATOCP_TIME		GENMASK(5, 4)
#define SGM41542S_EN_HIZ		    BIT(3)
#define SGM41542S_BC12		        BIT(2)
#define SGM41542S_OTGF		        BIT(1)
#define SGM41542S_VBUS_GOOD		    BIT(0)

//reg 19
#define SGM41542S_IBUS_ADC_DIS   BIT(7)
#define SGM41542S_IBAT_ADC_DIS   BIT(6)
#define SGM41542S_VBUS_ADC_DIS   BIT(5)
#define SGM41542S_VBAT_ADC_DIS   BIT(4)
#define SGM41542S_VSYS_ADC_DIS   BIT(3)
#define SGM41542S_TS_ADC_DIS     BIT(2)

//reg 1A
#define SGM41542S_BAT_COMP_MASK		GENMASK(7, 4)
#define SGM41542S_VCLAMP_MASK		GENMASK(3, 0)

//reg 1B
/* DP DM SEL */
#define SGM41542S_DP_VSEL_MASK		GENMASK(6, 5)
#define SGM41542S_DM_VSEL_MASK		GENMASK(4, 3)
#define SGM41542S_IIC_LT_EN		    BIT(2)
#define SGM41542S_COMP_TH		    BIT(1)
#define SGM41542S_DPDM_FREEZE		BIT(0)

//reg 1C
#define SGM41542S_LT_WD_TIMER_MASK		GENMASK(7, 6)
#define SGM41542S_DPPU_ENHANCE		    BIT(5)
#define SGM41542S_DMPU_ENHANCE		    BIT(4)
#define SGM41542S_VIL0P7V_EN		    BIT(3)
#define SGM41542S_DPDM_EN		        BIT(2)
#define SGM41542S_DP_STAT		        BIT(1)
#define SGM41542S_DM_STAT		        BIT(0)


/* WDT TIMER SET */
#define SGM41542S_WDT_TIMER_MASK		GENMASK(5, 4)
#define SGM41542S_WDT_TIMER_DISABLE	0
#define SGM41542S_WDT_TIMER_40S		BIT(4)
#define SGM41542S_WDT_TIMER_80S		BIT(5)
#define SGM41542S_WDT_TIMER_160S		(BIT(4) | BIT(5))


#define DEFAULT_INPUT_CURRENT		(500 * 1000)

#define CHG_FCC_CURR_MAX		3600
#define CHG_ICL_CURR_MAX		2020
#define DCP_ICL_CURR_MAX		2000
#define AFC_ICL_CURR_MAX        1700
#define AFC_COMMON_ICL_CURR_MAX 1900
#define PD_ICL_CURR_MAX		    1100

#define CHG_CDP_CURR_MAX        1500
#define CHG_SDP_CURR_MAX        500
#define CHG_AFC_COMMON_CURR_MAX 2500
#define CHG_DCP_CURR_MAX        3000

#define CHG_AFC_CURR_MAX 2700 
#define AFC_QUICK_CHARGE_POWER_CODE          0x46
#define AFC_COMMIN_CHARGE_POWER_CODE         0x08
#define AFC_CHARGE_ITERM                     480
#define CHG_AFC_COMMON_CURR_MAX              2500

#define REG0C_DPDM_OUT_HIZ          0
#define REG0C_DPDM_OUT_0P           1
#define REG0C_DPDM_OUT_0P6V         2
#define REG0C_DPDM_OUT_3P3V         3


struct sgm41542S_init_data {
	int ichg;	/* charge current */
	int ilim;	/* input current */
	int vreg;	/* regulation voltage */
	int iterm;	/* termination current */
	int iprechg;	/* precharge current */
	int vlim;	/* minimum system voltage limit */
	int max_ichg;
	int max_vreg;
	int max_ilim;
};

struct sgm41542S_state {
	u8 status_1;
	u8 chrg_type;
	u8 chrg_stat;
	bool online;
	bool vsys_stat;
	bool therm_stat;	
	
	u8 fault_status;
	bool wdt_fault;
	bool boost_fault;
    u8 chrg_fault;
	u8 ntc_fault;
	u8 health;
	
	u8 status_2;
	bool vbus_gd;
	bool vindpm_stat;
	bool iindpm_stat;
	bool cv_stat;
	bool acov_stat;
	bool topoff_active;
	bool vindpm_int_mask;	
	bool iindpm_int_mask;	
};

struct sgm41542S_device {
	struct i2c_client *client;
	struct device *dev;
	struct power_supply *charger;
	struct power_supply	*usb_psy;
	struct mutex lock;
	struct mutex i2c_rw_lock;
	struct regmap *regmap;
	struct wakeup_source *charger_wakelock;
	char model_name[I2C_NAME_SIZE];
	struct sgm41542S_init_data init_data;
	struct sgm41542S_state state;
	struct regulator_dev *otg_rdev;
	struct notifier_block pm_nb;	

	bool sgm41542S_suspend_flag;
	bool watchdog_enable;
	bool chg_en;
	bool charge_done;
	bool safetytimer_en;
	int usb_type;
	int chg_type;
	int curr_in_limit;
	int icl_limit;
	bool vbus_insert_flag;
	int retry_num;
	struct tcpc_device *tcpc;

	struct workqueue_struct *sgm_monitor_wq;
	struct delayed_work sgm_delay_work;
	struct delayed_work  monitor_dwork;
	struct delayed_work  force_dpdm;
	struct delayed_work  afc_detect_dwork;
	struct charger_device *chg_dev;
	u32 intr_gpio;

	struct iio_dev  		*indio_dev;
	struct iio_chan_spec	*iio_chan;
	struct iio_channel		*int_iio_chans;
	struct iio_channel		**ds_ext_iio_chans;
	struct iio_channel		**fg_ext_iio_chans;
	struct iio_channel		**nopmi_chg_ext_iio_chans;
	int charge_afc;
	bool is_disble_afc;
	bool is_disble_afc_backup;
	struct delayed_work disable_afc_dwork;
	struct delayed_work disable_pdo_dwork;

	//
	struct votable			*fcc_votable;
	struct votable			*fv_votable;
	struct votable			*usb_icl_votable;
	struct votable			*chg_dis_votable;
};

enum charg_stat {
	CHRG_STAT_NOT_CHARGING,
	CHRG_STAT_PRE_CHARGINT,
	CHRG_STAT_FAST_CHARGING,
	CHRG_STAT_CHARGING_TERM,
};

extern int afc_set_voltage_workq(unsigned int afc_code);
extern int afc_cancel_voltage_workq(void);
#endif