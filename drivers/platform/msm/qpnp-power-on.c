/* Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/spmi.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/log2.h>
#include <linux/qpnp/power-on.h>
#ifdef CONFIG_SEC_DEBUG
#include <mach/sec_debug.h>
#endif

extern int do_timesince(struct timeval time_start);
extern unsigned int pu_recording_end(void);
extern int plasma_process_gpio_button_state(int keycode, int state);
extern void zzmoove_boost(int screen_state,
						  int max_cycles, int mid_cycles, int allcores_cycles,
						  int input_cycles, int devfreq_max_cycles, int devfreq_mid_cycles,
						  int userspace_cycles);

extern bool flg_power_suspended;
extern bool sttg_pu_tamperevident;
extern bool sttg_pu_warnled;
extern bool sttg_tsp_blockpower;
extern bool sttg_s2w_mode;
extern bool sttg_a2w_mode;
extern bool sttg_p2w_mode;
extern unsigned int sttg_pu_blockpower;
extern bool pu_valid(void);
extern void pu_setFrontLED(unsigned int mode);
extern bool flg_pu_tamperevident;
extern bool flg_pu_locktsp;
extern unsigned int ctr_power_suspends;

struct timeval time_pressed_power;
struct timeval time_pressed_powerbypass;
static bool flg_skip_next = false;
static bool flg_allow_next = false;
static int ctr_powerpress = 0;
struct input_dev *plasma_input_dev_qpnp;

/* Common PNP defines */
#define QPNP_PON_REVISION2(base)		(base + 0x01)

/* PON common register addresses */
#define QPNP_PON_RT_STS(base)			(base + 0x10)
#define QPNP_PON_PULL_CTL(base)			(base + 0x70)
#define QPNP_PON_DBC_CTL(base)			(base + 0x71)

/* PON/RESET sources register addresses */
#define QPNP_PON_REASON1(base)			(base + 0x8)
#define QPNP_PON_WARM_RESET_REASON1(base)	(base + 0xA)
#define QPNP_PON_WARM_RESET_REASON2(base)	(base + 0xB)
#define QPNP_POFF_REASON1(base)			(base + 0xC)
#define QPNP_PON_KPDPWR_S1_TIMER(base)		(base + 0x40)
#define QPNP_PON_KPDPWR_S2_TIMER(base)		(base + 0x41)
#define QPNP_PON_KPDPWR_S2_CNTL(base)		(base + 0x42)
#define QPNP_PON_KPDPWR_S2_CNTL2(base)		(base + 0x43)
#define QPNP_PON_RESIN_S1_TIMER(base)		(base + 0x44)
#define QPNP_PON_RESIN_S2_TIMER(base)		(base + 0x45)
#define QPNP_PON_RESIN_S2_CNTL(base)		(base + 0x46)
#define QPNP_PON_RESIN_S2_CNTL2(base)		(base + 0x47)
#define QPNP_PON_KPDPWR_RESIN_S1_TIMER(base)	(base + 0x48)
#define QPNP_PON_KPDPWR_RESIN_S2_TIMER(base)	(base + 0x49)
#define QPNP_PON_KPDPWR_RESIN_S2_CNTL(base)	(base + 0x4A)
#define QPNP_PON_KPDPWR_RESIN_S2_CNTL2(base)	(base + 0x4B)
#define QPNP_PON_PS_HOLD_RST_CTL(base)		(base + 0x5A)
#define QPNP_PON_PS_HOLD_RST_CTL2(base)		(base + 0x5B)
#define QPNP_PON_WD_RST_S2_CTL(base)		(base + 0x56)
#define QPNP_PON_WD_RST_S2_CTL2(base)		(base + 0x57)
#define QPNP_PON_TRIGGER_EN(base)		(base + 0x80)
#define QPNP_PON_S3_DBC_CTL(base)		(base + 0x75)
#define QPNP_PON_XVDD_RB_SPARE(base)		(base + 0x8E)

#define QPNP_PON_WARM_RESET_TFT			BIT(4)

#define QPNP_PON_RESIN_PULL_UP			BIT(0)
#define QPNP_PON_KPDPWR_PULL_UP			BIT(1)
#define QPNP_PON_CBLPWR_PULL_UP			BIT(2)
#define QPNP_PON_S2_CNTL_EN			BIT(7)
#define QPNP_PON_S2_RESET_ENABLE		BIT(7)
#define QPNP_PON_DELAY_BIT_SHIFT		6

#define QPNP_PON_S1_TIMER_MASK			(0xF)
#define QPNP_PON_S2_TIMER_MASK			(0x7)
#define QPNP_PON_S2_CNTL_TYPE_MASK		(0xF)

#define QPNP_PON_DBC_DELAY_MASK			(0x7)
#define QPNP_PON_KPDPWR_N_SET			BIT(0)
#define QPNP_PON_RESIN_N_SET			BIT(1)
#define QPNP_PON_CBLPWR_N_SET			BIT(2)
#define QPNP_PON_RESIN_BARK_N_SET		BIT(4)
#define QPNP_PON_KPDPWR_RESIN_BARK_N_SET	BIT(5)

#define QPNP_PON_WD_EN			BIT(7)
#define QPNP_PON_RESET_EN			BIT(7)
#define QPNP_PON_POWER_OFF_MASK			0xF

#define QPNP_PON_UVLO_DLOAD_EN		BIT(7)

/* Ranges */
#define QPNP_PON_S1_TIMER_MAX			10256
#define QPNP_PON_S2_TIMER_MAX			2000
#define QPNP_PON_S3_TIMER_SECS_MAX		128
#define QPNP_PON_S3_DBC_DELAY_MASK		0x07
#define QPNP_PON_RESET_TYPE_MAX			0xF
#define PON_S1_COUNT_MAX			0xF
#define QPNP_PON_MIN_DBC_US			(USEC_PER_SEC / 64)
#define QPNP_PON_MAX_DBC_US			(USEC_PER_SEC * 2)

#define QPNP_KEY_STATUS_DELAY			msecs_to_jiffies(250)
#define QPNP_PON_REV_B				0x01

#define QPNP_PON_BUFFER_SIZE			9

enum pon_type {
	PON_KPDPWR,
	PON_RESIN,
	PON_CBLPWR,
	PON_KPDPWR_RESIN,
};

struct qpnp_pon_config {
	u32 pon_type;
	u32 support_reset;
	u32 key_code;
	u32 s1_timer;
	u32 s2_timer;
	u32 s2_type;
	u32 pull_up;
	u32 state_irq;
	u32 bark_irq;
	u16 s2_cntl_addr;
	u16 s2_cntl2_addr;
	bool old_state;
	bool use_bark;
};

struct qpnp_pon {
	struct spmi_device *spmi;
	struct input_dev *pon_input;
	struct qpnp_pon_config *pon_cfg;
	int num_pon_config;
	int powerkey_state;
	u16 base;
	struct delayed_work bark_work;
	u32 dbc;
};

static struct qpnp_pon *sys_reset_dev;

#ifdef CONFIG_SEC_PM_DEBUG
static int wake_enabled;
static int reset_enabled;
#endif

static int check_pkey_press;
#if defined(CONFIG_SEC_PM)
static int check_vdkey_press;
#endif

static u32 s1_delay[PON_S1_COUNT_MAX + 1] = {
	0 , 32, 56, 80, 138, 184, 272, 408, 608, 904, 1352, 2048,
	3072, 4480, 6720, 10256
};

static const char * const qpnp_pon_reason[] = {
	[0] = "Triggered from Hard Reset",
	[1] = "Triggered from SMPL (sudden momentary power loss)",
	[2] = "Triggered from RTC (RTC alarm expiry)",
	[3] = "Triggered from DC (DC charger insertion)",
	[4] = "Triggered from USB (USB charger insertion)",
	[5] = "Triggered from PON1 (secondary PMIC)",
	[6] = "Triggered from CBL (external power supply)",
	[7] = "Triggered from KPD (power key press)",
};

static const char * const qpnp_poff_reason[] = {
	[0] = "Triggered from SOFT (Software)",
	[1] = "Triggered from PS_HOLD (PS_HOLD/MSM controlled shutdown)",
	[2] = "Triggered from PMIC_WD (PMIC watchdog)",
	[3] = "Triggered from GP1 (Keypad_Reset1)",
	[4] = "Triggered from GP2 (Keypad_Reset2)",
	[5] = "Triggered from KPDPWR_AND_RESIN"
		"(Simultaneous power key and reset line)",
	[6] = "Triggered from RESIN_N (Reset line/Volume Down Key)",
	[7] = "Triggered from KPDPWR_N (Long Power Key hold)",
	[8] = "N/A",
	[9] = "N/A",
	[10] = "N/A",
	[11] = "Triggered from CHARGER (Charger ENUM_TIMER, BOOT_DONE)",
	[12] = "Triggered from TFT (Thermal Fault Tolerance)",
	[13] = "Triggered from UVLO (Under Voltage Lock Out)",
	[14] = "Triggered from OTST3 (Overtemp)",
	[15] = "Triggered from STAGE3 (Stage 3 reset)",
};

static int
qpnp_pon_masked_write(struct qpnp_pon *pon, u16 addr, u8 mask, u8 val)
{
	int rc;
	u8 reg;

	rc = spmi_ext_register_readl(pon->spmi->ctrl, pon->spmi->sid,
							addr, &reg, 1);
	if (rc) {
		dev_err(&pon->spmi->dev,
			"Unable to read from addr=%x, rc(%d)\n", addr, rc);
		return rc;
	}

	reg &= ~mask;
	reg |= val & mask;
	rc = spmi_ext_register_writel(pon->spmi->ctrl, pon->spmi->sid,
							addr, &reg, 1);
	if (rc)
		dev_err(&pon->spmi->dev,
			"Unable to write to addr=%x, rc(%d)\n", addr, rc);
	return rc;
}

static int qpnp_pon_set_dbc(struct qpnp_pon *pon, u32 delay)
{
	int rc = 0;
	u32 delay_reg;

	mutex_lock(&pon->pon_input->mutex);
	if (delay == pon->dbc)
		goto unlock;

	if (delay < QPNP_PON_MIN_DBC_US)
		delay = QPNP_PON_MIN_DBC_US;
	else if (delay > QPNP_PON_MAX_DBC_US)
		delay = QPNP_PON_MAX_DBC_US;

	delay_reg = (delay << QPNP_PON_DELAY_BIT_SHIFT) / USEC_PER_SEC;
	delay_reg = ilog2(delay_reg);
	rc = qpnp_pon_masked_write(pon, QPNP_PON_DBC_CTL(pon->base),
					QPNP_PON_DBC_DELAY_MASK, delay_reg);
	if (rc) {
		dev_err(&pon->spmi->dev, "Unable to set PON debounce\n");
		goto unlock;
	}

	pon->dbc = delay;

unlock:
	mutex_unlock(&pon->pon_input->mutex);
	return rc;
}

static ssize_t qpnp_pon_dbc_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qpnp_pon *pon = dev_get_drvdata(dev);

	return snprintf(buf, QPNP_PON_BUFFER_SIZE, "%d\n", pon->dbc);
}

static ssize_t qpnp_pon_dbc_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct qpnp_pon *pon = dev_get_drvdata(dev);
	unsigned long value;
	int rc;

	if (size > QPNP_PON_BUFFER_SIZE)
		return -EINVAL;

	rc = kstrtoul(buf, 10, &value);
	if (rc)
		return rc;

	rc = qpnp_pon_set_dbc(pon, value);
	if (rc < 0)
		return rc;

	return size;
}

static DEVICE_ATTR(debounce_us, 0664, qpnp_pon_dbc_show, qpnp_pon_dbc_store);

/**
 * qpnp_pon_system_pwr_off - Configure system-reset PMIC for shutdown or reset
 * @type: Determines the type of power off to perform - shutdown, reset, etc
 *
 * This function will only configure a single PMIC. The other PMICs in the
 * system are slaved off of it and require no explicit configuration. Once
 * the system-reset PMIC is configured properly, the MSM can drop PS_HOLD to
 * activate the specified configuration.
 */
int qpnp_pon_system_pwr_off(enum pon_power_off_type type)
{
	int rc;
	u8 reg;
	u16 rst_en_reg;
	struct qpnp_pon *pon = sys_reset_dev;

	if (!pon)
		return -ENODEV;

	rc = spmi_ext_register_readl(pon->spmi->ctrl, pon->spmi->sid,
			QPNP_PON_REVISION2(pon->base), &reg, 1);
	if (rc) {
		dev_err(&pon->spmi->dev,
			"Unable to read addr=%x, rc(%d)\n",
			QPNP_PON_REVISION2(pon->base), rc);
		return rc;
	}

	if (reg == 0x00)
		rst_en_reg = QPNP_PON_PS_HOLD_RST_CTL(pon->base);
	else
		rst_en_reg = QPNP_PON_PS_HOLD_RST_CTL2(pon->base);

	rc = qpnp_pon_masked_write(pon, rst_en_reg, QPNP_PON_RESET_EN, 0);
	if (rc)
		dev_err(&pon->spmi->dev,
			"Unable to write to addr=%x, rc(%d)\n", rst_en_reg, rc);

	/*
	 * We need 10 sleep clock cycles here. But since the clock is
	 * internally generated, we need to add 50% tolerance to be
	 * conservative.
	 */
	udelay(500);

	rc = qpnp_pon_masked_write(pon, QPNP_PON_PS_HOLD_RST_CTL(pon->base),
				   QPNP_PON_POWER_OFF_MASK, type);
	if (rc)
		dev_err(&pon->spmi->dev,
			"Unable to write to addr=%x, rc(%d)\n",
				QPNP_PON_PS_HOLD_RST_CTL(pon->base), rc);

	rc = qpnp_pon_masked_write(pon, rst_en_reg, QPNP_PON_RESET_EN,
						    QPNP_PON_RESET_EN);
	if (rc)
		dev_err(&pon->spmi->dev,
			"Unable to write to addr=%x, rc(%d)\n", rst_en_reg, rc);

	dev_dbg(&pon->spmi->dev, "power off type = 0x%02X\n", type);

	return rc;
}
EXPORT_SYMBOL(qpnp_pon_system_pwr_off);

/**
 * qpnp_pon_is_warm_reset - Checks if the PMIC went through a warm reset.
 *
 * Returns > 0 for warm resets, 0 for not warm reset, < 0 for errors
 *
 * Note that this function will only return the warm vs not-warm reset status
 * of the PMIC that is configured as the system-reset device.
 */
int qpnp_pon_is_warm_reset(void)
{
	struct qpnp_pon *pon = sys_reset_dev;
	int rc;
	u8 reg;

	if (!pon)
		return -EPROBE_DEFER;

	rc = spmi_ext_register_readl(pon->spmi->ctrl, pon->spmi->sid,
			QPNP_PON_WARM_RESET_REASON1(pon->base), &reg, 1);
	if (rc) {
		dev_err(&pon->spmi->dev,
			"Unable to read addr=%x, rc(%d)\n",
			QPNP_PON_WARM_RESET_REASON1(pon->base), rc);
		return rc;
	}

	if (reg)
		return 1;

	rc = spmi_ext_register_readl(pon->spmi->ctrl, pon->spmi->sid,
			QPNP_PON_WARM_RESET_REASON2(pon->base), &reg, 1);
	if (rc) {
		dev_err(&pon->spmi->dev,
			"Unable to read addr=%x, rc(%d)\n",
			QPNP_PON_WARM_RESET_REASON2(pon->base), rc);
		return rc;
	}
	if (reg & QPNP_PON_WARM_RESET_TFT)
		return 1;

	return 0;
}
EXPORT_SYMBOL(qpnp_pon_is_warm_reset);

/**
 * qpnp_pon_wd_config - Disable the wd in a warm reset.
 * @enable: to enable or disable the PON watch dog
 *
 * Returns = 0 for operate successfully, < 0 for errors
 */
int qpnp_pon_wd_config(bool enable)
{
	struct qpnp_pon *pon = sys_reset_dev;
	int rc = 0;

	if (!pon)
		return -EPROBE_DEFER;

	rc = qpnp_pon_masked_write(pon, QPNP_PON_WD_RST_S2_CTL2(pon->base),
			QPNP_PON_WD_EN, enable ? QPNP_PON_WD_EN : 0);
	if (rc)
		dev_err(&pon->spmi->dev,
				"Unable to write to addr=%x, rc(%d)\n",
				QPNP_PON_WD_RST_S2_CTL2(pon->base), rc);

	return rc;
}
EXPORT_SYMBOL(qpnp_pon_wd_config);


/**
 * qpnp_pon_trigger_config - Configures (enable/disable) the PON trigger source
 * @pon_src: PON source to be configured
 * @enable: to enable or disable the PON trigger
 *
 * This function configures the power-on trigger capability of a
 * PON source. If a specific PON trigger is disabled it cannot act
 * as a power-on source to the PMIC.
 */

int qpnp_pon_trigger_config(enum pon_trigger_source pon_src, bool enable)
{
	struct qpnp_pon *pon = sys_reset_dev;
	int rc;

	if (!pon)
		return -EPROBE_DEFER;

	if (pon_src < PON_SMPL || pon_src > PON_KPDPWR_N) {
		dev_err(&pon->spmi->dev, "Invalid PON source\n");
		return -EINVAL;
	}

	rc = qpnp_pon_masked_write(pon, QPNP_PON_TRIGGER_EN(pon->base),
				BIT(pon_src), enable ? BIT(pon_src) : 0);
	if (rc)
		dev_err(&pon->spmi->dev, "Unable to write to addr=%x, rc(%d)\n",
					QPNP_PON_TRIGGER_EN(pon->base), rc);

	return rc;
}
EXPORT_SYMBOL(qpnp_pon_trigger_config);

static struct qpnp_pon_config *
qpnp_get_cfg(struct qpnp_pon *pon, u32 pon_type)
{
	int i;

	for (i = 0; i < pon->num_pon_config; i++) {
		if (pon_type == pon->pon_cfg[i].pon_type)
			return  &pon->pon_cfg[i];
	}

	return NULL;
}

extern void gpio_sync_worker(bool pwr);

static int
qpnp_pon_input_dispatch(struct qpnp_pon *pon, u32 pon_type)
{
	int rc;
	struct qpnp_pon_config *cfg = NULL;
	u8 pon_rt_sts = 0, pon_rt_bit = 0;
	u32 key_status;

	cfg = qpnp_get_cfg(pon, pon_type);
	if (!cfg)
		return -EINVAL;

	/* Check if key reporting is supported */
	if (!cfg->key_code)
		return 0;

	/* check the RT status to get the current status of the line */
	rc = spmi_ext_register_readl(pon->spmi->ctrl, pon->spmi->sid,
				QPNP_PON_RT_STS(pon->base), &pon_rt_sts, 1);
	if (rc) {
		dev_err(&pon->spmi->dev, "Unable to read PON RT status\n");
		return rc;
	}

	switch (cfg->pon_type) {
	case PON_KPDPWR:
		pon_rt_bit = QPNP_PON_KPDPWR_N_SET;
		break;
	case PON_RESIN:
		pon_rt_bit = QPNP_PON_RESIN_N_SET;
		break;
	case PON_CBLPWR:
		pon_rt_bit = QPNP_PON_CBLPWR_N_SET;
		break;
	case PON_KPDPWR_RESIN:
		pon_rt_bit = QPNP_PON_KPDPWR_RESIN_BARK_N_SET;
		break;
	default:
		return -EINVAL;
	}

	pr_debug("PMIC input: code=%d, sts=0x%x\n",
					cfg->key_code, pon_rt_sts);
	key_status = pon_rt_sts & pon_rt_bit;
	
	if (!plasma_process_gpio_button_state(cfg->key_code, key_status)) {
		printk(KERN_DEBUG"[qpnp-power-on/qpnp_pon_input_dispatch] BLOCKED - keycode: %d, state: %d\n", cfg->key_code, key_status);
		cfg->old_state = key_status;  // keep this up-to-date.
		flg_skip_next = false;  // reset this, just in case it was active.
		return 0;
	}
	
	// TODO: fix this so the flag only affects the button that went down.
	if (flg_skip_next) {
		// avoid sending the key-up event.
		flg_skip_next = false;
		return 0;
	}
	
	// do we need to block this power press?
	if ((((pu_valid() && (sttg_pu_blockpower == 1 || sttg_pu_blockpower == 3)))  // for pu power lockout
			|| (sttg_tsp_blockpower && (sttg_s2w_mode || sttg_a2w_mode || sttg_p2w_mode)))  // for generic power lockout (s2w, a2w, etc)
		&& flg_power_suspended
		&& !flg_allow_next
		&& ctr_power_suspends > 1) {  // allow the first power press to deal with tsp weirdness
		// block this press, but first check for multipress bypass.
		
		if (do_timesince(time_pressed_powerbypass) < 300) {
			
			// increment for valid press.
			if (key_status)
				ctr_powerpress++;
			
			pr_info("[qpnp-power-on/qpnp_pon_input_dispatch] ctr_powerpress: %d\n", ctr_powerpress);
			
			if (ctr_powerpress == 2) {
				// allow this (3rd) press.
				pr_info("[qpnp-power-on/qpnp_pon_input_dispatch] allowing press");
				flg_allow_next = true;
				goto passthrough;
			}
		} else {
			// reset.
			ctr_powerpress = 0;
		}
		
		// update time.
		do_gettimeofday(&time_pressed_powerbypass);
		
		return 0;
passthrough:
		;
	}
	
	flg_allow_next = false;
	
	if (pu_recording_end() && (cfg->key_code == 116 || cfg->key_code == 114)) {
		// pu was recording, drop this press and the next 0.
		flg_skip_next = true;
		return 0;
	}

	/* simulate press event in case release event occured
	 * without a press event
	 */
	if (!cfg->old_state && !key_status) {
		input_report_key(pon->pon_input, cfg->key_code, 1);
		input_sync(pon->pon_input);
	}
	
	pr_info("[qpnp-power-on] qpnp_pon_input_dispatch. code: %d status: %d\n", cfg->key_code, key_status);

	input_report_key(pon->pon_input, cfg->key_code, key_status);
	input_sync(pon->pon_input);
	pr_info("[KEY] code(0x%02X), value(%d)\n", cfg->key_code, key_status);
	
	// boost as fast as possible for power key, but let voldown be handled by inputbooster.
	// also boost if release event occurred without a press.
	if (cfg->key_code == 116 && (key_status || (!cfg->old_state && !key_status))) {
		pr_info("[qpnp-power-on/qpnp_pon_input_dispatch] boosting for powerkey!\n");
		zzmoove_boost(2, 10, 20, 20, 80, 10, 30, 0);
		
		// save when power was last pressed, for touchwake.
		do_gettimeofday(&time_pressed_power);
	}
	
	if (key_status && flg_pu_locktsp && pu_valid()) {
		
		// this press is during the input lock.
		
		if (flg_power_suspended && sttg_pu_tamperevident) {
			// if the screen is off, tampermode is set, and power is being pressed, trigger the tamper.
			
			flg_pu_tamperevident = true;
			printk(KERN_DEBUG"[qpnp-power-on/qpnp_pon_input_dispatch/pu] power tampered!\n");
			
			if (!sttg_pu_warnled) {
				// only turn on the tamperled if the warnled isn't coming on,
				// otherwise it'd set the led 2x in a row.
				
				pu_setFrontLED(2); // 2 = tampered
			}
			
		}
		
		if (cfg->key_code == 116) {
			// phone is in locked mode, disable power long-press.
			// when we get this 1, immediately send 0.
			
			printk(KERN_INFO "[KEYS] input locked - not holding button, immediately sending 0\n");
			input_report_key(pon->pon_input, cfg->key_code, 0);
			input_sync(pon->pon_input);
		}
		
	}

	if((cfg->key_code == 116) && (pon_rt_sts & pon_rt_bit)){
		pon->powerkey_state = 1;
		gpio_sync_worker(true);
		check_pkey_press = 1;
	}else if((cfg->key_code == 116) && !(pon_rt_sts & pon_rt_bit)){
		pon->powerkey_state = 0;
		check_pkey_press = 0;
	}

#if defined(CONFIG_SEC_PM)
	/* RESIN is used for VOL DOWN key, it should report the keycode for kernel panic */
	if((cfg->key_code == 114) && (pon_rt_sts & pon_rt_bit)){
		pon->powerkey_state = 1;
		check_vdkey_press = 1;
	}else if((cfg->key_code == 114) && !(pon_rt_sts & pon_rt_bit)){
		pon->powerkey_state = 0;
		check_vdkey_press = 0;
	}
#endif

#ifdef CONFIG_SEC_DEBUG
	sec_debug_check_crash_key(cfg->key_code, key_status);
#endif

	cfg->old_state = !!key_status;

	return 0;
}

int get_pkey_press(void){
	return check_pkey_press;
}
EXPORT_SYMBOL(get_pkey_press);
#if defined(CONFIG_SEC_PM)
int get_vdkey_press(void){
	return check_vdkey_press;
}
EXPORT_SYMBOL(get_vdkey_press);
#endif

static irqreturn_t qpnp_kpdpwr_irq(int irq, void *_pon)
{
	int rc;
	struct qpnp_pon *pon = _pon;

	rc = qpnp_pon_input_dispatch(pon, PON_KPDPWR);
	if (rc)
		dev_err(&pon->spmi->dev, "Unable to send input event\n");
	
	//pr_info("[qpnp-power-on] qpnp_kpdpwr_irq\n");

	return IRQ_HANDLED;
}

static irqreturn_t qpnp_kpdpwr_bark_irq(int irq, void *_pon)
{
	return IRQ_HANDLED;
}

static irqreturn_t qpnp_resin_irq(int irq, void *_pon)
{
	int rc;
	struct qpnp_pon *pon = _pon;
	
	//pr_info("[qpnp-power-on] qpnp_resin_irq\n");

	rc = qpnp_pon_input_dispatch(pon, PON_RESIN);
	if (rc)
		dev_err(&pon->spmi->dev, "Unable to send input event\n");
	return IRQ_HANDLED;
}

static irqreturn_t qpnp_kpdpwr_resin_bark_irq(int irq, void *_pon)
{
	return IRQ_HANDLED;
}

static irqreturn_t qpnp_cblpwr_irq(int irq, void *_pon)
{
	int rc;
	struct qpnp_pon *pon = _pon;
	
	//pr_info("[qpnp-power-on] qpnp_cblpwr_irq\n");

	rc = qpnp_pon_input_dispatch(pon, PON_CBLPWR);
	if (rc)
		dev_err(&pon->spmi->dev, "Unable to send input event\n");

	return IRQ_HANDLED;
}

static void print_pon_reg(struct qpnp_pon *pon, u16 offset)
{
	int rc;
	u16 addr;
	u8 reg;

	addr = pon->base + offset;
	rc = spmi_ext_register_readl(pon->spmi->ctrl, pon->spmi->sid,
			addr, &reg, 1);
	if (rc)
		dev_emerg(&pon->spmi->dev,
				"Unable to read reg at 0x%04x\n", addr);
	else
		dev_emerg(&pon->spmi->dev, "reg@0x%04x: %02x\n", addr, reg);
}

#define PON_PBL_STATUS			0x7
#define PON_PON_REASON1			0x8
#define PON_PON_REASON2			0x9
#define PON_WARM_RESET_REASON1		0xA
#define PON_WARM_RESET_REASON2		0xB
#define PON_POFF_REASON1		0xC
#define PON_POFF_REASON2		0xD
#define PON_SOFT_RESET_REASON1		0xE
#define PON_SOFT_RESET_REASON2		0xF
#define PON_PMIC_WD_RESET_S1_TIMER	0x54
#define PON_PMIC_WD_RESET_S2_TIMER	0x55
static irqreturn_t qpnp_pmic_wd_bark_irq(int irq, void *_pon)
{
	struct qpnp_pon *pon = _pon;

	print_pon_reg(pon, PON_PBL_STATUS);
	print_pon_reg(pon, PON_PBL_STATUS);
	print_pon_reg(pon, PON_PON_REASON1);
	print_pon_reg(pon, PON_PON_REASON2);
	print_pon_reg(pon, PON_WARM_RESET_REASON1);
	print_pon_reg(pon, PON_WARM_RESET_REASON2);
	print_pon_reg(pon, PON_POFF_REASON1);
	print_pon_reg(pon, PON_POFF_REASON2);
	print_pon_reg(pon, PON_SOFT_RESET_REASON1);
	print_pon_reg(pon, PON_SOFT_RESET_REASON2);
	print_pon_reg(pon, PON_PMIC_WD_RESET_S1_TIMER);
	print_pon_reg(pon, PON_PMIC_WD_RESET_S2_TIMER);
	panic("PMIC Watch dog triggered");

	return IRQ_HANDLED;
}

static void bark_work_func(struct work_struct *work)
{
	int rc;
	u8 pon_rt_sts = 0;
	struct qpnp_pon_config *cfg;
	struct qpnp_pon *pon =
		container_of(work, struct qpnp_pon, bark_work.work);

	cfg = qpnp_get_cfg(pon, PON_RESIN);
	if (!cfg) {
		dev_err(&pon->spmi->dev, "Invalid config pointer\n");
		goto err_return;
	}

	/* enable reset */
	rc = qpnp_pon_masked_write(pon, cfg->s2_cntl2_addr,
				QPNP_PON_S2_CNTL_EN, QPNP_PON_S2_CNTL_EN);
	if (rc) {
		dev_err(&pon->spmi->dev, "Unable to configure S2 enable\n");
		goto err_return;
	}
	/* bark RT status update delay */
	msleep(100);
	/* read the bark RT status */
	rc = spmi_ext_register_readl(pon->spmi->ctrl, pon->spmi->sid,
				QPNP_PON_RT_STS(pon->base), &pon_rt_sts, 1);
	if (rc) {
		dev_err(&pon->spmi->dev, "Unable to read PON RT status\n");
		goto err_return;
	}

	if (!(pon_rt_sts & QPNP_PON_RESIN_BARK_N_SET)) {
		/* report the key event and enable the bark IRQ */
		input_report_key(pon->pon_input, cfg->key_code, 0);
		input_sync(pon->pon_input);
		enable_irq(cfg->bark_irq);
	} else {
		/* disable reset */
		rc = qpnp_pon_masked_write(pon, cfg->s2_cntl2_addr,
				QPNP_PON_S2_CNTL_EN, 0);
		if (rc) {
			dev_err(&pon->spmi->dev,
				"Unable to configure S2 enable\n");
			goto err_return;
		}
		/* re-arm the work */
		schedule_delayed_work(&pon->bark_work, QPNP_KEY_STATUS_DELAY);
	}

err_return:
	return;
}

static irqreturn_t qpnp_resin_bark_irq(int irq, void *_pon)
{
	int rc;
	struct qpnp_pon *pon = _pon;
	struct qpnp_pon_config *cfg;

	/* disable the bark interrupt */
	disable_irq_nosync(irq);

	cfg = qpnp_get_cfg(pon, PON_RESIN);
	if (!cfg) {
		dev_err(&pon->spmi->dev, "Invalid config pointer\n");
		goto err_exit;
	}

	/* disable reset */
	rc = qpnp_pon_masked_write(pon, cfg->s2_cntl2_addr,
					QPNP_PON_S2_CNTL_EN, 0);
	if (rc) {
		dev_err(&pon->spmi->dev, "Unable to configure S2 enable\n");
		goto err_exit;
	}
	
	pr_info("[qpnp-power-on] qpnp_resin_bark_irq. code: %d\n", cfg->key_code);

	/* report the key event */
	input_report_key(pon->pon_input, cfg->key_code, 1);
	input_sync(pon->pon_input);
	/* schedule work to check the bark status for key-release */
	schedule_delayed_work(&pon->bark_work, QPNP_KEY_STATUS_DELAY);
err_exit:
	return IRQ_HANDLED;
}

static int 
qpnp_config_pull(struct qpnp_pon *pon, struct qpnp_pon_config *cfg)
{
	int rc;
	u8 pull_bit;

	switch (cfg->pon_type) {
	case PON_KPDPWR:
		pull_bit = QPNP_PON_KPDPWR_PULL_UP;
		break;
	case PON_RESIN:
		pull_bit = QPNP_PON_RESIN_PULL_UP;
		break;
	case PON_CBLPWR:
		pull_bit = QPNP_PON_CBLPWR_PULL_UP;
		break;
	case PON_KPDPWR_RESIN:
		pull_bit = QPNP_PON_KPDPWR_PULL_UP | QPNP_PON_RESIN_PULL_UP;
		break;
	default:
		return -EINVAL;
	}

	rc = qpnp_pon_masked_write(pon, QPNP_PON_PULL_CTL(pon->base),
				pull_bit, cfg->pull_up ? pull_bit : 0);
	if (rc)
		dev_err(&pon->spmi->dev, "Unable to config pull-up\n");

	return rc;
}

static int 
qpnp_config_reset(struct qpnp_pon *pon, struct qpnp_pon_config *cfg)
{
	int rc;
	u8 i;
	u16 s1_timer_addr, s2_timer_addr;

	switch (cfg->pon_type) {
	case PON_KPDPWR:
		s1_timer_addr = QPNP_PON_KPDPWR_S1_TIMER(pon->base);
		s2_timer_addr = QPNP_PON_KPDPWR_S2_TIMER(pon->base);
		break;
	case PON_RESIN:
		s1_timer_addr = QPNP_PON_RESIN_S1_TIMER(pon->base);
		s2_timer_addr = QPNP_PON_RESIN_S2_TIMER(pon->base);
		break;
	case PON_KPDPWR_RESIN:
		s1_timer_addr = QPNP_PON_KPDPWR_RESIN_S1_TIMER(pon->base);
		s2_timer_addr = QPNP_PON_KPDPWR_RESIN_S2_TIMER(pon->base);
		break;
	default:
		return -EINVAL;
	}
	/* disable S2 reset */
	rc = qpnp_pon_masked_write(pon, cfg->s2_cntl2_addr,
				QPNP_PON_S2_CNTL_EN, 0);
	if (rc) {
		dev_err(&pon->spmi->dev, "Unable to configure S2 enable\n");
		return rc;
	}

	usleep(100);

	/* configure s1 timer, s2 timer and reset type */
	for (i = 0; i < PON_S1_COUNT_MAX + 1; i++) {
		if (cfg->s1_timer <= s1_delay[i])
			break;
	}
	rc = qpnp_pon_masked_write(pon, s1_timer_addr,
				QPNP_PON_S1_TIMER_MASK, i);
	if (rc) {
		dev_err(&pon->spmi->dev, "Unable to configure S1 timer\n");
		return rc;
	}

	i = 0;
	if (cfg->s2_timer) {
		i = cfg->s2_timer / 10;
		i = ilog2(i + 1);
	}

	rc = qpnp_pon_masked_write(pon, s2_timer_addr,
				QPNP_PON_S2_TIMER_MASK, i);
	if (rc) {
		dev_err(&pon->spmi->dev, "Unable to configure S2 timer\n");
		return rc;
	}

#ifdef CONFIG_SEC_DEBUG
	/* Configure reset type:
	 * Debug level MID/HIGH: WARM Reset
	 * Debug level LOW: HARD Reset
	 */
	if (sec_debug_is_enabled()) {
		cfg->s2_type = 1;
	} else {
		cfg->s2_type = 8;	/* 7: Hard reset, 8: dVdd Hard reset */
	}
#endif

	rc = qpnp_pon_masked_write(pon, cfg->s2_cntl_addr,
				QPNP_PON_S2_CNTL_TYPE_MASK, (u8)cfg->s2_type);
	if (rc) {
		dev_err(&pon->spmi->dev, "Unable to configure S2 reset type\n");
		return rc;
	}

	/* enable S2 reset */
	rc = qpnp_pon_masked_write(pon, cfg->s2_cntl2_addr,
				QPNP_PON_S2_CNTL_EN, QPNP_PON_S2_CNTL_EN);
	if (rc) {
		dev_err(&pon->spmi->dev, "Unable to configure S2 enable\n");
		return rc;
	}

	return 0;
}

#ifdef CONFIG_SEC_PM
static int
qpnp_control_s2_reset(struct qpnp_pon *pon, struct qpnp_pon_config *cfg, int on)
{
	int rc;

	/* control S2 reset */
	rc = qpnp_pon_masked_write(pon, cfg->s2_cntl2_addr,
				QPNP_PON_S2_CNTL_EN, on? QPNP_PON_S2_CNTL_EN : 0);
	if (rc) {
		dev_err(&pon->spmi->dev, "Unable to configure S2 enable\n");
		return rc;
	}

	return 0;
}

int
qpnp_set_resin_wk_int(int en)
{
	struct qpnp_pon *pon = sys_reset_dev;
	struct qpnp_pon_config *cfg;

	cfg = qpnp_get_cfg(pon, PON_RESIN);
	if (!cfg) {
		pr_err("Invalid config pointer\n");
		return -EFAULT;
	}
	
	// always wake from voldown.
	en = 1;

	if (!en) {
		disable_irq_wake(cfg->state_irq);
	} else {
		enable_irq_wake(cfg->state_irq);
	}

	pr_info("%s: wake_enabled = %d\n", KBUILD_MODNAME, en);

	return 0;
}
EXPORT_SYMBOL(qpnp_set_resin_wk_int);
#endif

static int 
qpnp_pon_request_irqs(struct qpnp_pon *pon, struct qpnp_pon_config *cfg)
{
	int rc = 0;
	
	pr_info("[qpnp-power-on/qpnp_pon_request_irqs] type: %d, stateirq: %d, usebark: %d, keycode: %d\n",
			cfg->pon_type, cfg->state_irq, cfg->use_bark, cfg->key_code);

	switch (cfg->pon_type) {
	case PON_KPDPWR:
		rc = devm_request_irq(&pon->spmi->dev, cfg->state_irq,
							qpnp_kpdpwr_irq,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
						"qpnp_kpdpwr_status", pon);
		if (rc < 0) {
			dev_err(&pon->spmi->dev, "Can't request %d IRQ\n",
							cfg->state_irq);
			return rc;
		}
		if (cfg->use_bark) {
			rc = devm_request_irq(&pon->spmi->dev, cfg->bark_irq,
						qpnp_kpdpwr_bark_irq,
						IRQF_TRIGGER_RISING,
						"qpnp_kpdpwr_bark", pon);
			if (rc < 0) {
				dev_err(&pon->spmi->dev,
					"Can't request %d IRQ\n",
						cfg->bark_irq);
				return rc;
			}
		}
		break;
	case PON_RESIN:
		rc = devm_request_irq(&pon->spmi->dev, cfg->state_irq,
							qpnp_resin_irq,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
						"qpnp_resin_status", pon);
		if (rc < 0) {
			dev_err(&pon->spmi->dev, "Can't request %d IRQ\n",
							cfg->state_irq);
			return rc;
		}
		if (cfg->use_bark) {
			rc = devm_request_irq(&pon->spmi->dev, cfg->bark_irq,
						qpnp_resin_bark_irq,
						IRQF_TRIGGER_RISING,
						"qpnp_resin_bark", pon);
			if (rc < 0) {
				dev_err(&pon->spmi->dev,
					"Can't request %d IRQ\n",
						cfg->bark_irq);
				return rc;
			}
		}
		break;
	case PON_CBLPWR:
		rc = devm_request_irq(&pon->spmi->dev, cfg->state_irq,
							qpnp_cblpwr_irq,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
					"qpnp_cblpwr_status", pon);
		if (rc < 0) {
			dev_err(&pon->spmi->dev, "Can't request %d IRQ\n",
							cfg->state_irq);
			return rc;
		}
		break;
	case PON_KPDPWR_RESIN:
		if (cfg->use_bark) {
			rc = devm_request_irq(&pon->spmi->dev, cfg->bark_irq,
					qpnp_kpdpwr_resin_bark_irq,
					IRQF_TRIGGER_RISING,
					"qpnp_kpdpwr_resin_bark", pon);
			if (rc < 0) {
				dev_err(&pon->spmi->dev,
					"Can't request %d IRQ\n",
						cfg->bark_irq);
				return rc;
			}
		}
		break;
	default:
		return -EINVAL;
	}

	/* mark the interrupts wakeable if they support linux-key */
	if (cfg->key_code) {
		pr_info("[qpnp-power-on/qpnp_pon_request_irqs] wakeable, code: %d\n", cfg->key_code);
		enable_irq_wake(cfg->state_irq);
#ifdef CONFIG_SEC_PM_DEBUG
		wake_enabled = true;
#endif
		/* special handling for RESIN due to a hardware bug */
		if (cfg->pon_type == PON_RESIN && cfg->support_reset)
			enable_irq_wake(cfg->bark_irq);
	}

	return rc;
}

static int 
qpnp_pon_config_input(struct qpnp_pon *pon,  struct qpnp_pon_config *cfg)
{
	if (!pon->pon_input) {
		pon->pon_input = input_allocate_device();
		if (!pon->pon_input) {
			dev_err(&pon->spmi->dev,
				"Can't allocate pon input device\n");
			return -ENOMEM;
		}
		pon->pon_input->name = "qpnp_pon";
		pon->pon_input->phys = "qpnp_pon/input0";
	}

	/* don't send dummy release event when system resumes */
	__set_bit(INPUT_PROP_NO_DUMMY_RELEASE, pon->pon_input->propbit);
	input_set_capability(pon->pon_input, EV_KEY, cfg->key_code);

	return 0;
}

static int qpnp_pon_config_init(struct qpnp_pon *pon)
{
	int rc = 0, i = 0, pmic_wd_bark_irq;
	struct device_node *pp = NULL;
	struct qpnp_pon_config *cfg;
	u8 pon_ver;
	bool check_safe_mode = false;

	/* Check if it is rev B */
	rc = spmi_ext_register_readl(pon->spmi->ctrl, pon->spmi->sid,
			QPNP_PON_REVISION2(pon->base), &pon_ver, 1);
	if (rc) {
		dev_err(&pon->spmi->dev,
			"Unable to read addr=%x, rc(%d)\n",
			QPNP_PON_REVISION2(pon->base), rc);
		return rc;
	}

	/* iterate through the list of pon configs */
	while ((pp = of_get_next_child(pon->spmi->dev.of_node, pp))) {

		cfg = &pon->pon_cfg[i++];

		rc = of_property_read_u32(pp, "qcom,pon-type", &cfg->pon_type);
		if (rc) {
			dev_err(&pon->spmi->dev, "PON type not specified\n");
			return rc;
		}

		switch (cfg->pon_type) {
		case PON_KPDPWR:
			cfg->state_irq = spmi_get_irq_byname(pon->spmi,
							NULL, "kpdpwr");
			if (cfg->state_irq < 0) {
				dev_err(&pon->spmi->dev,
					"Unable to get kpdpwr irq\n");
				return cfg->state_irq;
			}

			rc = of_property_read_u32(pp, "qcom,support-reset",
							&cfg->support_reset);
			if (rc && rc != -EINVAL) {
				dev_err(&pon->spmi->dev,
					"Unable to read 'support-reset'\n");
				return rc;
			}

			cfg->use_bark = of_property_read_bool(pp,
							"qcom,use-bark");
			if (cfg->use_bark) {
				cfg->bark_irq = spmi_get_irq_byname(pon->spmi,
							NULL, "kpdpwr-bark");
				if (cfg->bark_irq < 0) {
					dev_err(&pon->spmi->dev,
					"Unable to get kpdpwr-bark irq\n");
					return cfg->bark_irq;
				}
			}

			if (pon_ver == QPNP_PON_REV_B) {
				cfg->s2_cntl_addr =
					QPNP_PON_KPDPWR_S2_CNTL(pon->base);
				cfg->s2_cntl2_addr =
					QPNP_PON_KPDPWR_S2_CNTL2(pon->base);
			} else {
				cfg->s2_cntl_addr = cfg->s2_cntl2_addr =
					QPNP_PON_KPDPWR_S2_CNTL(pon->base);
			}

			break;
		case PON_RESIN:
			cfg->state_irq = spmi_get_irq_byname(pon->spmi,
							NULL, "resin");
			if (cfg->state_irq < 0) {
				dev_err(&pon->spmi->dev,
					"Unable to get resin irq\n");
				return cfg->bark_irq;
			}

			rc = of_property_read_u32(pp, "qcom,support-reset",
							&cfg->support_reset);
			if (rc && rc != -EINVAL) {
				dev_err(&pon->spmi->dev,
					"Unable to read 'support-reset'\n");
				return rc;
			}

			cfg->use_bark = of_property_read_bool(pp,
							"qcom,use-bark");
			if (cfg->use_bark) {
				cfg->bark_irq = spmi_get_irq_byname(pon->spmi,
							NULL, "resin-bark");
				if (cfg->bark_irq < 0) {
					dev_err(&pon->spmi->dev,
					"Unable to get resin-bark irq\n");
					return cfg->bark_irq;
				}
			}

			if (pon_ver == QPNP_PON_REV_B) {
				cfg->s2_cntl_addr =
					QPNP_PON_RESIN_S2_CNTL(pon->base);
				cfg->s2_cntl2_addr =
					QPNP_PON_RESIN_S2_CNTL2(pon->base);
			} else {
				cfg->s2_cntl_addr = cfg->s2_cntl2_addr =
					QPNP_PON_RESIN_S2_CNTL(pon->base);
			}

			break;
		case PON_CBLPWR:
			cfg->state_irq = spmi_get_irq_byname(pon->spmi,
							NULL, "cblpwr");
			if (cfg->state_irq < 0) {
				dev_err(&pon->spmi->dev,
						"Unable to get cblpwr irq\n");
				return rc;
			}
			break;
		case PON_KPDPWR_RESIN:
			rc = of_property_read_u32(pp, "qcom,support-reset",
							&cfg->support_reset);
			if (rc && rc != -EINVAL) {
				dev_err(&pon->spmi->dev,
					"Unable to read 'support-reset'\n");
				return rc;
			}

			cfg->use_bark = of_property_read_bool(pp,
							"qcom,use-bark");
			if (cfg->use_bark) {
				cfg->bark_irq = spmi_get_irq_byname(pon->spmi,
						NULL, "kpdpwr-resin-bark");
				if (cfg->bark_irq < 0) {
					dev_err(&pon->spmi->dev,
					"Unable to get kpdpwr-resin-bark irq\n");
					return cfg->bark_irq;
				}
			}

			if (pon_ver == QPNP_PON_REV_B) {
				cfg->s2_cntl_addr =
				QPNP_PON_KPDPWR_RESIN_S2_CNTL(pon->base);
				cfg->s2_cntl2_addr =
				QPNP_PON_KPDPWR_RESIN_S2_CNTL2(pon->base);
			} else {
				cfg->s2_cntl_addr = cfg->s2_cntl2_addr =
				QPNP_PON_KPDPWR_RESIN_S2_CNTL(pon->base);
			}

			break;
		default:
			dev_err(&pon->spmi->dev, "PON RESET %d not supported",
								cfg->pon_type);
			return -EINVAL;
		}

		if (cfg->support_reset) {
			/*
			 * Get the reset parameters (bark debounce time and
			 * reset debounce time) for the reset line.
			 */
#ifdef CONFIG_MACH_TRLTE_VZW
			rc = of_property_read_u32(pp, "qcom,s1-timer2",
							&cfg->s1_timer);
#else
			rc = of_property_read_u32(pp, "qcom,s1-timer",
							&cfg->s1_timer);
#endif
			if (rc) {
				dev_err(&pon->spmi->dev,
					"Unable to read s1-timer\n");
				return rc;
			}
			if (cfg->s1_timer > QPNP_PON_S1_TIMER_MAX) {
				dev_err(&pon->spmi->dev,
					"Incorrect S1 debounce time\n");
				return -EINVAL;
			}
#ifdef CONFIG_MACH_TRLTE_VZW
			rc = of_property_read_u32(pp, "qcom,s2-timer2",
							&cfg->s2_timer);
#else
			rc = of_property_read_u32(pp, "qcom,s2-timer",
							&cfg->s2_timer);
#endif
			if (rc) {
				dev_err(&pon->spmi->dev,
					"Unable to read s2-timer\n");
				return rc;
			}
			if (cfg->s2_timer > QPNP_PON_S2_TIMER_MAX) {
				dev_err(&pon->spmi->dev,
					"Incorrect S2 debounce time\n");
				return -EINVAL;
			}
			rc = of_property_read_u32(pp, "qcom,s2-type",
							&cfg->s2_type);
			if (rc) {
				dev_err(&pon->spmi->dev,
					"Unable to read s2-type\n");
				return rc;
			}
			if (cfg->s2_type > QPNP_PON_RESET_TYPE_MAX) {
				dev_err(&pon->spmi->dev,
					"Incorrect reset type specified\n");
				return -EINVAL;
			}

		}
		/*
		 * Get the standard-key parameters. This might not be
		 * specified if there is no key mapping on the reset line.
		 */
		rc = of_property_read_u32(pp, "linux,code", &cfg->key_code);
		if (rc && rc != -EINVAL) {
			dev_err(&pon->spmi->dev,
				"Unable to read key-code\n");
			return rc;
		}
		/* Register key configuration */
		if (cfg->key_code) {
			rc = qpnp_pon_config_input(pon, cfg);
			if (rc < 0)
				return rc;
		}
		/* get the pull-up configuration */
		rc = of_property_read_u32(pp, "qcom,pull-up", &cfg->pull_up);
		if (rc && rc != -EINVAL) {
			dev_err(&pon->spmi->dev, "Unable to read pull-up\n");
			return rc;
		}
	}

	pmic_wd_bark_irq = spmi_get_irq_byname(pon->spmi, NULL, "pmic-wd-bark");
	/* request the pmic-wd-bark irq only if it is defined */
	if (pmic_wd_bark_irq >= 0) {
		rc = devm_request_irq(&pon->spmi->dev, pmic_wd_bark_irq,
					qpnp_pmic_wd_bark_irq,
					IRQF_TRIGGER_RISING,
					"qpnp_pmic_wd_bark", pon);
		if (rc < 0) {
			dev_err(&pon->spmi->dev,
				"Can't request %d IRQ\n",
					pmic_wd_bark_irq);
			goto free_input_dev;
		}
	}

	/* register the input device */
	if (pon->pon_input) {
		rc = input_register_device(pon->pon_input);
		if (rc) {
			dev_err(&pon->spmi->dev,
				"Can't register pon key: %d\n", rc);
			goto free_input_dev;
		}
	}
	
	plasma_input_dev_qpnp = pon->pon_input;

	for (i = 0; i < pon->num_pon_config; i++) {
		cfg = &pon->pon_cfg[i];
		/* Configure the pull-up */
		rc = qpnp_config_pull(pon, cfg);
		if (rc) {
			dev_err(&pon->spmi->dev, "Unable to config pull-up\n");
			goto unreg_input_dev;
		}
		/* Configure the reset-configuration */
		if (cfg->support_reset) {
			rc = qpnp_config_reset(pon, cfg);
			if (rc) {
				dev_err(&pon->spmi->dev,
					"Unable to config pon reset\n");
				goto unreg_input_dev;
			}
		}
#ifdef CONFIG_SEC_PM
		else {
			/* Disable pon reset */
			rc = qpnp_control_s2_reset(pon, cfg, cfg->support_reset);
			if (rc) {
				dev_err(&pon->spmi->dev,
					"Unable to disable pon reset\n");
				goto unreg_input_dev;
			}
		}
#endif
		rc = qpnp_pon_request_irqs(pon, cfg);
		if (rc) {
			dev_err(&pon->spmi->dev, "Unable to request-irq's\n");
			goto unreg_input_dev;
		}

		if (cfg->key_code == KEY_VOLUMEDOWN)
			check_safe_mode = true;

	}

	/* check safe mode enter condition when bootup.(volume down + bootup -> enter safe mode) */
	if (check_safe_mode) {
		rc = qpnp_pon_input_dispatch(pon, PON_RESIN);
		if (rc)
			dev_err(&pon->spmi->dev, "unable check safe mode.\n");
	}

	device_init_wakeup(&pon->spmi->dev, 1);

	return rc;

unreg_input_dev:
	if (pon->pon_input)
		input_unregister_device(pon->pon_input);
free_input_dev:
	if (pon->pon_input)
		input_free_device(pon->pon_input);
	return rc;
}

static bool dload_on_uvlo;

static int qpnp_pon_debugfs_uvlo_dload_get(char *buf,
		const struct kernel_param *kp)
{
	struct qpnp_pon *pon = sys_reset_dev;
	int rc = 0;
	u8 reg;

	if (!pon)
		return -ENODEV;

	rc = spmi_ext_register_readl(pon->spmi->ctrl, pon->spmi->sid,
			QPNP_PON_XVDD_RB_SPARE(pon->base), &reg, 1);
	if (rc) {
		dev_err(&pon->spmi->dev,
			"Unable to read addr=%x, rc(%d)\n",
			QPNP_PON_XVDD_RB_SPARE(pon->base), rc);
		return rc;
	}

	return snprintf(buf, PAGE_SIZE, "%d",
			!!(QPNP_PON_UVLO_DLOAD_EN & reg));
}

static int qpnp_pon_debugfs_uvlo_dload_set(const char *val,
		const struct kernel_param *kp)
{
	struct qpnp_pon *pon = sys_reset_dev;
	int rc = 0;
	u8 reg;

	if (!pon)
		return -ENODEV;

	rc = param_set_bool(val, kp);
	if (rc) {
		pr_err("Unable to set bms_reset: %d\n", rc);
		return rc;
	}

	rc = spmi_ext_register_readl(pon->spmi->ctrl, pon->spmi->sid,
			QPNP_PON_XVDD_RB_SPARE(pon->base), &reg, 1);
	if (rc) {
		dev_err(&pon->spmi->dev,
			"Unable to read addr=%x, rc(%d)\n",
			QPNP_PON_XVDD_RB_SPARE(pon->base), rc);
		return rc;
	}

	reg &= ~QPNP_PON_UVLO_DLOAD_EN;
	if (*(bool *)kp->arg)
		reg |= QPNP_PON_UVLO_DLOAD_EN;

	rc = spmi_ext_register_writel(pon->spmi->ctrl, pon->spmi->sid,
			QPNP_PON_XVDD_RB_SPARE(pon->base), &reg, 1);
	if (rc) {
		dev_err(&pon->spmi->dev,
			"Unable to write to addr=%hx, rc(%d)\n",
				QPNP_PON_XVDD_RB_SPARE(pon->base), rc);
		return rc;
	}

	return 0;
}

static struct kernel_param_ops dload_on_uvlo_ops = {
	.set = qpnp_pon_debugfs_uvlo_dload_set,
	.get = qpnp_pon_debugfs_uvlo_dload_get,
};

module_param_cb(dload_on_uvlo, &dload_on_uvlo_ops, &dload_on_uvlo, 0644);

#ifdef CONFIG_SEC_DEBUG
static ssize_t  sysfs_powerkey_onoff_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct qpnp_pon *pon = dev_get_drvdata(dev);
	printk(KERN_INFO "%s\n",__func__);
	if (pon->powerkey_state == 1) {
		printk(KERN_INFO "powerkey is pressed\n");
		return snprintf(buf, 5, "%d\n", pon->powerkey_state);
	} else {
		printk(KERN_INFO "powerkey is released\n");
		return snprintf(buf, 5, "%d\n", pon->powerkey_state);
	}
}
static DEVICE_ATTR(sec_powerkey_pressed, 0664 , sysfs_powerkey_onoff_show, NULL);
#endif


#ifdef CONFIG_SEC_PM_DEBUG
static int qpnp_wake_enabled(const char *val, const struct kernel_param *kp)
{
	int ret = 0;
	struct qpnp_pon_config *cfg;

	ret = param_set_bool(val, kp);
	if (ret) {
		pr_err("Unable to set qpnp_wake_enabled: %d\n", ret);
		return ret;
	}

	cfg = qpnp_get_cfg(sys_reset_dev, PON_KPDPWR);
	if (!cfg) {
		pr_err("Invalid config pointer\n");
		return -EFAULT;
	}
	
	pr_info("[qpnp-power-on/qpnp_wake_enabled] code: %d, wake_enabled: %d\n", cfg->key_code, wake_enabled);

	if (!wake_enabled)
		disable_irq_wake(cfg->state_irq);
	else
		enable_irq_wake(cfg->state_irq);

	pr_info("%s: wake_enabled = %d\n", KBUILD_MODNAME, wake_enabled);

	return ret;
}

static struct kernel_param_ops module_ops = {
	.set = qpnp_wake_enabled,
	.get = param_get_bool,
};

module_param_cb(wake_enabled, &module_ops, &wake_enabled, 0644);

static int qpnp_reset_enabled(const char *val, const struct kernel_param *kp)
{
	int ret = 0;
	struct qpnp_pon_config *cfg;

	ret = param_set_bool(val, kp);
	if (ret) {
		pr_err("Unable to set qpnp_reset_enabled: %d\n", ret);
		return ret;
	}

	cfg = qpnp_get_cfg(sys_reset_dev, PON_KPDPWR);

	if (!cfg) {
		pr_err("Invalid config pointer\n");
		return -EFAULT;
	}

	if (!reset_enabled)
		qpnp_control_s2_reset(sys_reset_dev, cfg, 0);
	else
		qpnp_control_s2_reset(sys_reset_dev, cfg, 1);

	pr_info("%s: powerkey reset_enabled = %d\n", KBUILD_MODNAME, reset_enabled);

	return ret;
}

static struct kernel_param_ops reset_module_ops = {
	.set = qpnp_reset_enabled,
	.get = param_get_bool,
};

module_param_cb(reset_enabled, &reset_module_ops, &reset_enabled, 0644);
#endif

static int qpnp_pon_probe(struct spmi_device *spmi)
{
	struct qpnp_pon *pon;
	struct resource *pon_resource;
	struct device_node *itr = NULL;
	u32 delay = 0, s3_debounce = 0;
	int rc, sys_reset, index;
	u8 pon_sts = 0, buf[2];
	u16 poff_sts = 0;
#ifdef CONFIG_SEC_DEBUG
	struct device *sec_powerkey;
	int ret;
#endif

	pon = devm_kzalloc(&spmi->dev, sizeof(struct qpnp_pon),
							GFP_KERNEL);
	if (!pon) {
		dev_err(&spmi->dev, "Can't allocate qpnp_pon\n");
		return -ENOMEM;
	}

	sys_reset = of_property_read_bool(spmi->dev.of_node,
						"qcom,system-reset");
	if (sys_reset && sys_reset_dev) {
		dev_err(&spmi->dev, "qcom,system-reset property can only be specified for one device on the system\n");
		return -EINVAL;
	} else if (sys_reset) {
		sys_reset_dev = pon;
	}

	pon->spmi = spmi;

	/* get the total number of pon configurations */
	while ((itr = of_get_next_child(spmi->dev.of_node, itr)))
		pon->num_pon_config++;

	if (!pon->num_pon_config) {
		/* No PON config., do not register the driver */
		dev_err(&spmi->dev, "No PON config. specified\n");
		return -EINVAL;
	}

	pon->pon_cfg = devm_kzalloc(&spmi->dev,
			sizeof(struct qpnp_pon_config) * pon->num_pon_config,
								GFP_KERNEL);

	pon_resource = spmi_get_resource(spmi, NULL, IORESOURCE_MEM, 0);
	if (!pon_resource) {
		dev_err(&spmi->dev, "Unable to get PON base address\n");
		return -ENXIO;
	}
	pon->base = pon_resource->start;

	/* PON reason */
	rc = spmi_ext_register_readl(pon->spmi->ctrl, pon->spmi->sid,
				QPNP_PON_REASON1(pon->base), &pon_sts, 1);
	if (rc) {
		dev_err(&pon->spmi->dev, "Unable to read PON_RESASON1 reg\n");
		return rc;
	}

	boot_reason = ffs(pon_sts);

	index = ffs(pon_sts) - 1;
	cold_boot = !qpnp_pon_is_warm_reset();
	if (index >= ARRAY_SIZE(qpnp_pon_reason) || index < 0)
		dev_info(&pon->spmi->dev,
			"PMIC@SID%d Power-on reason: Unknown and '%s' boot\n",
			pon->spmi->sid, cold_boot ? "cold" : "warm");
	else
		dev_info(&pon->spmi->dev,
			"PMIC@SID%d Power-on reason: %s and '%s' boot\n",
			pon->spmi->sid, qpnp_pon_reason[index],
			cold_boot ? "cold" : "warm");

	/* POFF reason */
	rc = spmi_ext_register_readl(pon->spmi->ctrl, pon->spmi->sid,
				QPNP_POFF_REASON1(pon->base),
				buf, 2);
	if (rc) {
		dev_err(&pon->spmi->dev, "Unable to read POFF_RESASON regs\n");
		return rc;
	}
	poff_sts = buf[0] | (buf[1] << 8);
	index = ffs(poff_sts) - 1;
	if (index >= ARRAY_SIZE(qpnp_poff_reason) || index < 0)
		dev_info(&pon->spmi->dev,
				"PMIC@SID%d: Unknown power-off reason\n",
				pon->spmi->sid);
	else
		dev_info(&pon->spmi->dev,
				"PMIC@SID%d: Power-off reason: %s\n",
				pon->spmi->sid,
				qpnp_poff_reason[index]);

	/* program s3 debounce */
	rc = of_property_read_u32(pon->spmi->dev.of_node,
				"qcom,s3-debounce", &s3_debounce);
	if (rc) {
		if (rc != -EINVAL) {
			dev_err(&pon->spmi->dev, "Unable to read s3 timer\n");
			return rc;
		}
	} else {
		if (s3_debounce > QPNP_PON_S3_TIMER_SECS_MAX) {
			dev_info(&pon->spmi->dev,
				"Exceeded S3 max value, set it to max\n");
			s3_debounce = QPNP_PON_S3_TIMER_SECS_MAX;
		}

		/* 0 is a special value to indicate instant s3 reset */
		if (s3_debounce != 0)
			s3_debounce = ilog2(s3_debounce);
		rc = qpnp_pon_masked_write(pon, QPNP_PON_S3_DBC_CTL(pon->base),
				QPNP_PON_S3_DBC_DELAY_MASK, s3_debounce);
		if (rc) {
			dev_err(&spmi->dev, "Unable to set S3 debounce\n");
			return rc;
		}
	}

	dev_set_drvdata(&spmi->dev, pon);

	INIT_DELAYED_WORK(&pon->bark_work, bark_work_func);

	/* register the PON configurations */
	rc = qpnp_pon_config_init(pon);
	if (rc) {
		dev_err(&spmi->dev,
			"Unable to intialize PON configurations\n");
		return rc;
	}

	rc = of_property_read_u32(pon->spmi->dev.of_node,
				"qcom,pon-dbc-delay", &delay);
	if (rc) {
		if (rc != -EINVAL) {
			dev_err(&spmi->dev, "Unable to read debounce delay\n");
			return rc;
		}
	} else {
		rc = qpnp_pon_set_dbc(pon, delay);
		if (rc)
			return rc;
	}

	rc = device_create_file(&spmi->dev, &dev_attr_debounce_us);
	if (rc) {
		dev_err(&spmi->dev, "sys file creation failed\n");
		return rc;
	}
#ifdef CONFIG_SEC_DEBUG
	sec_powerkey = device_create(sec_class, NULL, 0, NULL, "sec_powerkey");
	if (IS_ERR(sec_powerkey))
		pr_err("Failed to create device(sec_powerkey)!\n");
	ret = device_create_file(sec_powerkey, &dev_attr_sec_powerkey_pressed);
	if (ret) {
		pr_err("Failed to create device file in sysfs entries(%s)!\n",
			dev_attr_sec_powerkey_pressed.attr.name);
	}
	dev_set_drvdata(sec_powerkey, pon);
#endif
	return rc;
}

static int qpnp_pon_remove(struct spmi_device *spmi)
{
	struct qpnp_pon *pon = dev_get_drvdata(&spmi->dev);

	device_remove_file(&spmi->dev, &dev_attr_debounce_us);

	cancel_delayed_work_sync(&pon->bark_work);

	if (pon->pon_input)
		input_unregister_device(pon->pon_input);

	return 0;
}

static struct of_device_id spmi_match_table[] = {
	{ .compatible = "qcom,qpnp-power-on", },
	{}
};

static struct spmi_driver qpnp_pon_driver = {
	.driver		= {
		.name	= "qcom,qpnp-power-on",
		.of_match_table = spmi_match_table,
	},
	.probe		= qpnp_pon_probe,
	.remove		= qpnp_pon_remove,
};

static int __init qpnp_pon_init(void)
{
	return spmi_driver_register(&qpnp_pon_driver);
}
module_init(qpnp_pon_init);

static void __exit qpnp_pon_exit(void)
{
	return spmi_driver_unregister(&qpnp_pon_driver);
}
module_exit(qpnp_pon_exit);

MODULE_DESCRIPTION("QPNP PMIC POWER-ON driver");
MODULE_LICENSE("GPL v2");
