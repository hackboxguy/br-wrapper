// SPDX-License-Identifier: GPL-2.0
/*
 * HH983 FPDLink Serializer Driver
 *
 * Supports three configurations:
 *   Mode 0: DS90UH983 + DS90UH984 (REM_INTB forwarding)
 *   Mode 1: DS90UH983 + DS90UH988 (I2C passthrough for TDDI + REM_INTB)
 *   Mode 2: DS90UH983 + DS90Ux988, video only (no touch, no interrupts)
 *
 * Author: Albert David
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>

/* Configuration mode: 0=983+984, 1=983+988, 2=983+988 video only */
static int config_mode = 0;
module_param(config_mode, int, 0444);
MODULE_PARM_DESC(config_mode, "Configuration mode: 0=983+984, 1=983+988, 2=983+988 video only, no touch (default: 0)");

/* Link status poll interval (0 = disable monitoring) */
static int poll_interval_ms = 1000;
static struct hh983_data *hh983_poll_owner;	/* device whose poll follows the parameter */
static int hh983_set_poll_interval(const char *val, const struct kernel_param *kp);
static const struct kernel_param_ops hh983_poll_ops = {
	.set = hh983_set_poll_interval,
	.get = param_get_int,
};
module_param_cb(poll_interval_ms, &hh983_poll_ops, &poll_interval_ms, 0644);
MODULE_PARM_DESC(poll_interval_ms, "Link status poll interval in ms (0=disable, default: 1000); writing a positive value restarts a stopped poll");

/* Mode 0 (983+984) DP video guard.
 *
 * Without DP input video the 983 timing generator keeps running but
 * stretches every line while it waits for data.  The 984 forwards that
 * distorted timing to the eDP panel: after ~5 s the 984 DTG wedges
 * (measured line length stays wrong after video returns) and after ~10 s
 * an eDP panel TCON can latch black until a power cycle.  The guard
 * disables the 984 main video stream while the 983 VP is out of sync so
 * the panel sees an idle link, then pulses the 984 DTG reset and
 * re-enables the stream once the VP has resynced.  Verified on the
 * OTS-OLED 17.3 (2880x1620) with 25 s, 60 s and Pi-reboot outages.
 */
static int dp_guard = 1;
module_param(dp_guard, int, 0444);
MODULE_PARM_DESC(dp_guard, "Mode 0 only: 1=cut 984 video stream on DP video loss, restore after 983 resync (default: 1), 0=off");

/* Mode 0 (983+984) DTG-wedge check, the guard's second failure mode.
 *
 * The guard above keys on the 983 losing DP input video.  Twice on the
 * OTS-OLED bench (2026-09-08 and both black screens of 2026-09-10) the panel
 * went black without any such loss: the 983 VP stayed synced, the 984 main
 * stream stayed on, and only the 984 DTG's *measured* input line length
 * wandered off -- 4400..6300 and once 5313, against a programmed 3440.  The
 * panel latches black about ten seconds later and only a power cycle brings it
 * back, so it has to be caught inside the panel's ~5 s tolerance.
 *
 * The measurement and the recovery already exist here: the restore path reads
 * both H totals and compares them, and it knows the safe order (cut the
 * stream, pulse the DTG only while it is off, settle, enable).  This check
 * simply runs that comparison on every poll while video is up, and calls the
 * same restore.  Two consecutive bad polls are required, and a holdoff keeps a
 * genuinely broken link from looping the pulse.
 */
static int dtg_check = 1;
module_param(dtg_check, int, 0444);
MODULE_PARM_DESC(dtg_check, "Mode 0 dp_guard only: 1=also restore when the 984 DTG wedges with no video loss (default: 1), 0=off");

/* A wedged 984 DTG is off by more than a thousand pixels; a healthy one
 * measures 3441..3443 against a programmed 3440, so 32 leaves room for the
 * measurement's own jitter without hiding a real wedge. */
#define DP_GUARD_HTOTAL_TOL_DEFAULT  32
static int dtg_tolerance = DP_GUARD_HTOTAL_TOL_DEFAULT;
module_param(dtg_tolerance, int, 0644);
MODULE_PARM_DESC(dtg_tolerance, "Mode 0 dp_guard: |measured - programmed| H total, in pixels, before the 984 DTG counts as wedged (default: 32)");

static int wedge_holdoff_s = 30;
module_param(wedge_holdoff_s, int, 0644);
MODULE_PARM_DESC(wedge_holdoff_s, "Mode 0 dp_guard: minimum seconds between two DTG-wedge restores (default: 30)");

static int dtg_wedge_count;
module_param(dtg_wedge_count, int, 0444);
MODULE_PARM_DESC(dtg_wedge_count, "Mode 0 dp_guard: DTG wedges detected since load (read-only)");

/*
 * Whether a detected wedge is acted on.
 *
 * The recovery is a DTG reset pulse, and a pulse is known to be able to
 * black-latch this panel: forcing one on a healthy DTG on 2026-09-10 did
 * exactly that. What is not known is whether the pulse is also what latches the
 * panel during a *real* wedge, or whether the wedge would have taken it anyway.
 * Three observations do not separate the two -- one real wedge was pulsed and
 * the panel stayed lit, a later burst of two was pulsed and it did not.
 *
 * dtg_recover=0 detects and logs without pulsing, which answers that question:
 * if the panel goes black on a wedge that was never pulsed, the wedge takes the
 * panel and the recovery is worth keeping; if it stays lit, the pulse is
 * implicated. The VP-sync resync path is untouched either way, so a genuine
 * video loss still recovers normally.
 *
 * Default 1: this is an experiment, not a change of behaviour.
 */
static int dtg_recover = 1;
module_param(dtg_recover, int, 0644);
MODULE_PARM_DESC(dtg_recover, "Mode 0 dp_guard: 1=pulse the DTG to recover a detected wedge (default), 0=detect and log only");

/* Mode 0 (983+984) OLED-OTS touch controller routing.
 *
 * On the OLED-OTS 17.3 board the HX8530 TDDI touch controller is on the
 * DS90UB984's SECOND local I2C bus (I2C_SDA1/SCL1 = "Port 1"), physically
 * at address 0x49.  Plain I2C pass-through (reg 0x07) only reaches the 984's
 * Port 0, so the touch is invisible to the host by default.  A 983
 * target-alias entry hops the transaction over the back channel to the
 * deserializer's I2C Port 1 (TARGET_DEST=0x20, datasheet SNLS608 Table 7-45)
 * and remaps the physical 0x49 to the host-visible 0x48 the himax driver
 * probes.  Off by default so other 983+984 boards are unchanged.
 */
static int ots_touch;
module_param(ots_touch, int, 0444);
MODULE_PARM_DESC(ots_touch, "Mode 0 only: 1=route the OLED-OTS HX8530 touch (984 I2C Port 1, phys 0x49) to host 0x48 (default: 0)");

/* HX8530 on the 984's local I2C Port 1: physical address vs host-visible alias. */
#define OTS_TOUCH_PHYS_ADDR	0x49	/* actual 7-bit addr on the 984 Port 1 bus */
#define OTS_TOUCH_HOST_ADDR	0x48	/* address presented to the Pi (himax DT reg) */

/* Common serializer registers */
#define SER_RESET_CTL            0x01  /* Reset control */
#define SER_I2C_CONTROL          0x07
#define SER_GENERAL_STS          0x0C  /* Link status (RO): [6]=RX_LOCK, [4]=LINK_LOST, [0]=LINK_DET */
#define SER_GPIO4_CONFIG         0x1B
#define SER_APB_CTL              0x48  /* APB indirect access control */
#define SER_APB_ADR0             0x49  /* APB address low byte */
#define SER_APB_ADR1             0x4A  /* APB address high byte */
#define SER_APB_DATA0            0x4B  /* APB data byte 0 */
#define SER_INTERRUPT_CTL        0x51  /* Interrupt enable: [7]=INTB_PIN_EN [4]=IE_DP_RX0 */
#define SER_TARGET_ID0           0x70
#define SER_TARGET_ID1           0x71
#define SER_TARGET_ALIAS0        0x78
#define SER_TARGET_ALIAS1        0x79
#define SER_TARGET_DEST0         0x88
#define SER_TARGET_DEST1         0x89
#define SER_INTERRUPT_CTRL       0xC6
#define SER_IND_ACC_CTL          0x40  /* [5:2]=page, [1]=auto-inc, [0]=read strobe */
#define SER_IND_ACC_ADDR         0x41
#define SER_IND_ACC_DATA         0x42
#define SER_IND_PAGE_VP          0x0C  /* Video processor 0..3 registers (script byte 0x32) */
#define SER_VP0_STS              0x30  /* VP_STS_VP0: [0]=TIMING_GEN_STS synced to input video */

/* Serializer configuration values */
#define SER_ENABLE_PASSTHROUGH   0xD8
#define SER_ENABLE_REM_INT       0x21
#define SER_GPIO4_PORT0_REM_INT  0x88
#define SER_GPIO4_PORT1_REM_INT  0x98
#define SER_ENABLE_GLOBAL_INT    0x93  /* INTB_PIN_EN + IE_DP_RX0 + IE_FPD_TX1 + IE_FPD_TX0 */
#define SER_ENABLE_GLOBAL_INT_NO_DP 0x83  /* INTB_PIN_EN + IE_FPD_TX1 + IE_FPD_TX0 (no DP RX int) */
#define SER_DIGITAL_RESET_0      0x01  /* bit 0 of RESET_CTL: self-clearing digital reset, preserves regs */

/* APB_CTL field values */
#define APB_ENABLE               0x01  /* bit 0: enable APB access */
#define APB_READ                 0x02  /* bit 1: start APB read (W1S, self-clears when done) */

/* APB register addresses (DP RX block, APB_SELECT=0) */
#define APB_LINK_ENABLE          0x000 /* bit 0: 1=HPD HIGH + RX enabled, 0=HPD LOW */
#define APB_SINK_0_INT_MASK      0x190 /* Sink 0 interrupt mask (default 0x79 = most masked) */
#define APB_SINK_0_INT_CAUSE     0x194 /* Sink 0 interrupt cause (read-clear):
                                        *   [2]=NO_VIDEO  [1]=VIDEO_DETECT  [0]=VIDEO_MODE_CHANGE */

/* 984 Deserializer registers */
#define DES984_GENERAL_CFG       0x04  /* I2C pass-through control (default 0xC1) */
#define DES984_GPIO4_PIN_CTL     0x19  /* GPIO4 pin control (RX Lock indicator) */
#define DES984_GPIO6_PIN_CTL     0x1B  /* GPIO6 pin control (Combined Lock indicator) */
#define DES984_INTB_ENABLE       0x44
#define DES984_GP_STATUS_0       0x53  /* [0]=FPD4RX_LOCK [1]=FPD3RX_LOCK [2]=FPDTX_PLL_LOCK */
#define DES984_GP_STATUS_1       0x54  /* [0]=LOCK [6]=FPDRX_PLL_LOCK (no SIG_DET) */
#define DES984_INTB_VALUE        0x81
/* 984 local display timing generator and DP TX (same indirect/APB scheme as 983) */
#define DES984_IND_PAGE_DTG      0x14  /* DTG page (script byte 0x50) */
#define DES984_DTG_P0_CTL        0x32  /* Port 0 DTG control */
#define DES984_DTG_P1_CTL        0x62  /* Port 1 DTG control */
#define DES984_DTG_HOLD_RESET    0x06
#define DES984_DTG_RELEASE       0x04
#define DES984_DTG_MEAS_HTOTAL_HI 0x40 /* Measured input H total, 15-bit big-endian */
#define DES984_DTG_MEAS_HTOTAL_LO 0x41
#define DES984_DTG_MEAS_VTOTAL_HI 0x42 /* Measured input V total, 15-bit big-endian */
#define DES984_DTG_MEAS_VTOTAL_LO 0x43
#define DES984_APB_MAIN_STREAM_EN 0x0084 /* DP TX main video stream enable (1=on) */
#define SER_VP0_H_TOTAL_LO       0x16  /* VID_H_TOTAL0_VP0 (programmed output H total) */
#define SER_VP0_H_TOTAL_HI       0x17
/* The H-total tolerance is the dtg_tolerance module parameter above, so the
 * restore path and the wedge check cannot drift apart. */

/* DP guard poll debouncing (in poll_interval_ms units) */
#define DP_GUARD_LOSS_POLLS      2     /* consecutive unsynced polls before cutting the stream */
#define DP_GUARD_RESYNC_POLLS    2     /* consecutive synced polls before restoring the stream */
#define DP_GUARD_WEDGE_POLLS     2     /* consecutive out-of-tolerance polls before calling it a wedge */

/* 984 configuration values */
#define DES984_ENABLE_PASSTHROUGH 0xC9  /* GENERAL_CFG default 0xC1 | bit[3] I2C_PASS_THROUGH */
#define DES984_GPIO_FORCED_LOW    0xC0  /* Output enabled, Device Status, fixed output 0 */
#define DES984_GPIO4_RX_LOCK      0x9C  /* Port 0 RX Lock Detect (default) */
#define DES984_GPIO6_COMBINED_LOCK 0xC2 /* Mode-dependent Lock indication (default) */

/* 988 Deserializer registers */
#define DES988_I2C_CONTROL       0x04
#define DES988_GPIO4_PIN_CTL     0x19  /* GPIO4 pin control (LOCK0 on display driver board) */
#define DES988_GPIO6_PIN_CTL     0x1B  /* GPIO6 pin control (LOCK_DUAL on display driver board) */
#define DES988_RX_INTN_CTL       0x44  /* INTB_IN enable register (datasheet 7.3.9) */
#define DES988_GP_STATUS_0       0x53  /* [0]=FPD4_LOCK, [1]=FPD3_LOCK, [2]=FPDTX_PLL_LOCK */
#define DES988_GP_STATUS_1       0x54  /* [0]=LOCK, [1]=SIG_DET, [6]=FPD_PLL_LOCK */

/* 988 Deserializer configuration values */
#define DES988_ENABLE_PASSTHROUGH 0xD9
#define DES988_INTB_IN_ENABLE    0x81  /* 0x81 required, not 0x80! Enables INTB_IN -> REM_INTB forwarding */

/* 988 GPIO pin modes — GPIO4/GPIO6 drive reset signals on the display driver board.
 * Toggling LOW then restoring to lock-indicator mode simulates FPDLink cable unplug/replug.
 */
#define DES988_GPIO_FORCED_LOW   0xC0  /* Output enabled, forced LOW (resets display driver) */
#define DES988_GPIO4_RX_LOCK     0x9C  /* Port 0 RX Lock indicator (default) */
#define DES988_GPIO6_COMBINED_LOCK 0xC2 /* Combined lock indicator (default) */

/* TDDI I2C targets (7-bit addresses) */
#define TDDI_ADDR_1              0x48
#define TDDI_ADDR_2              0x49

/* TARGET_ID/ALIAS format: (7-bit addr << 1) */
#define MAKE_TARGET_ID(addr)     ((addr) << 1)

/* TARGET_DEST format: [7:5]=port, [1:0]=depth */
#define TARGET_DEST_PORT0        0x00
#define TARGET_DEST_PORT1        0x20

struct hh983_data {
	struct i2c_client *client;
	u8 deser_addr;
	int mode;
	bool initialized;
	/* Link monitoring */
	struct delayed_work link_work;
	bool link_up;
	int recovery_count;
	int recovery_cooldown;  /* poll cycles to skip after recovery */
	int down_count;         /* consecutive polls with link down */
	/* Mode 0 DP video guard */
	bool guard_video_up;    /* last known 983 VP0 sync state */
	int guard_up_count;     /* consecutive synced polls while down */
	bool guard_stream_cut;  /* 984 main stream currently disabled by the guard */
	/* Mode 0 DTG-wedge check (video up, DTG measurement wrong) */
	bool guard_wedged;           /* currently in the wedged state */
	int guard_dtg_count;         /* consecutive out-of-tolerance polls */
	unsigned long guard_wedge_at;/* jiffies of the last wedge restore */
	bool guard_wedge_armed;      /* guard_wedge_at holds a real timestamp */
};

static int hh983_write_reg(struct i2c_client *client, u8 reg, u8 value)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, reg, value);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to write reg 0x%02X: %d\n", reg, ret);
		return ret;
	}
	dev_dbg(&client->dev, "SER 0x%02X <- 0x%02X\n", reg, value);
	return 0;
}

static int hh983_read_reg(struct i2c_client *client, u8 reg)
{
	int ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0)
		dev_err(&client->dev, "Failed to read reg 0x%02X: %d\n", reg, ret);
	else
		dev_dbg(&client->dev, "SER 0x%02X = 0x%02X\n", reg, ret);
	return ret;
}

static int hh983_write_deser_reg(struct i2c_client *client, u8 deser_addr, u8 reg, u8 value)
{
	struct i2c_msg msg;
	u8 buf[2];
	int ret;

	buf[0] = reg;
	buf[1] = value;

	msg.addr = deser_addr;
	msg.flags = 0;
	msg.len = 2;
	msg.buf = buf;

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret != 1) {
		dev_err(&client->dev, "Failed to write DES[0x%02X] reg 0x%02X: %d\n",
			deser_addr, reg, ret);
		return ret < 0 ? ret : -EIO;
	}
	dev_dbg(&client->dev, "DES[0x%02X] 0x%02X <- 0x%02X\n", deser_addr, reg, value);
	return 0;
}

static int hh983_read_deser_reg(struct i2c_client *client, u8 deser_addr, u8 reg)
{
	struct i2c_msg msgs[2];
	u8 reg_buf = reg;
	u8 val_buf;
	int ret;

	msgs[0].addr = deser_addr;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &reg_buf;

	msgs[1].addr = deser_addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = 1;
	msgs[1].buf = &val_buf;

	ret = i2c_transfer(client->adapter, msgs, 2);
	if (ret != 2) {
		dev_err(&client->dev, "Failed to read DES[0x%02X] reg 0x%02X: %d\n",
			deser_addr, reg, ret);
		return ret < 0 ? ret : -EIO;
	}
	dev_dbg(&client->dev, "DES[0x%02X] 0x%02X = 0x%02X\n", deser_addr, reg, val_buf);
	return val_buf;
}

/* Write to 983 APB register (indirect access to DP RX block) */
static int hh983_apb_write(struct i2c_client *client, u16 apb_addr, u8 data)
{
	int ret;

	ret = hh983_write_reg(client, SER_APB_ADR0, apb_addr & 0xFF);
	if (ret < 0)
		return ret;
	ret = hh983_write_reg(client, SER_APB_ADR1, (apb_addr >> 8) & 0xFF);
	if (ret < 0)
		return ret;
	ret = hh983_write_reg(client, SER_APB_DATA0, data);
	if (ret < 0)
		return ret;
	ret = hh983_write_reg(client, SER_APB_CTL, APB_ENABLE);
	if (ret < 0)
		return ret;

	dev_dbg(&client->dev, "APB 0x%03X <- 0x%02X\n", apb_addr, data);
	return 0;
}

/* Read byte 0 (bits 7:0) from 983 APB register.
 * Procedure per datasheet Table 7-119:
 *   1. Set APB_ADR0/ADR1 with target address
 *   2. Write APB_CTL with APB_ENABLE | APB_READ to start the read
 *   3. APB_READ bit (W1S) self-clears when read completes
 *   4. Read result from APB_DATA0
 */
static int hh983_apb_read(struct i2c_client *client, u16 apb_addr)
{
	int ret;

	ret = hh983_write_reg(client, SER_APB_ADR0, apb_addr & 0xFF);
	if (ret < 0)
		return ret;
	ret = hh983_write_reg(client, SER_APB_ADR1, (apb_addr >> 8) & 0xFF);
	if (ret < 0)
		return ret;
	ret = hh983_write_reg(client, SER_APB_CTL, APB_ENABLE | APB_READ);
	if (ret < 0)
		return ret;
	/* APB_READ self-clears when the internal read completes;
	 * allow time for the APB transaction to finish.
	 */
	usleep_range(100, 200);

	return hh983_read_reg(client, SER_APB_DATA0);
}

/* Log link status from both serializer and deserializer */
static void hh983_check_link_status(struct hh983_data *data)
{
	struct i2c_client *client = data->client;
	int ser_sts;

	ser_sts = hh983_read_reg(client, SER_GENERAL_STS);
	if (ser_sts >= 0)
		dev_info(&client->dev, "SER GENERAL_STS=0x%02X [%s%s%s]\n", ser_sts,
			 (ser_sts & 0x40) ? "RX_LOCK " : "",
			 (ser_sts & 0x10) ? "LINK_LOST " : "",
			 (ser_sts & 0x01) ? "LINK_DET" : "NO_LINK");

	if (data->mode == 1 || data->mode == 2) {
		/* Mode 2 is a 988 too, so the same status registers apply. */
		int des_sts0, des_sts1;

		des_sts0 = hh983_read_deser_reg(client, data->deser_addr, DES988_GP_STATUS_0);
		des_sts1 = hh983_read_deser_reg(client, data->deser_addr, DES988_GP_STATUS_1);
		if (des_sts0 >= 0 && des_sts1 >= 0)
			dev_info(&client->dev, "DES STS0=0x%02X STS1=0x%02X [%s%s%s]\n",
				 des_sts0, des_sts1,
				 (des_sts0 & 0x01) ? "FPD4_LOCK " : "",
				 (des_sts1 & 0x02) ? "SIG_DET " : "",
				 (des_sts1 & 0x01) ? "LOCK" : "NO_LOCK");
	} else if (data->mode == 0) {
		int des_sts0, des_sts1;

		des_sts0 = hh983_read_deser_reg(client, data->deser_addr, DES984_GP_STATUS_0);
		des_sts1 = hh983_read_deser_reg(client, data->deser_addr, DES984_GP_STATUS_1);
		if (des_sts0 >= 0 && des_sts1 >= 0)
			dev_info(&client->dev, "DES STS0=0x%02X STS1=0x%02X [%s%s%s]\n",
				 des_sts0, des_sts1,
				 (des_sts0 & 0x01) ? "FPD4_LOCK " : "",
				 (des_sts1 & 0x40) ? "PLL_LOCK " : "",
				 (des_sts1 & 0x01) ? "LOCK" : "NO_LOCK");
	}
}

/* Read a 983 indirect-page register (page select in IND_ACC_CTL[5:2], read strobe bit 0). */
static int hh983_ind_read(struct i2c_client *client, u8 page, u8 offset)
{
	int ret;

	ret = hh983_write_reg(client, SER_IND_ACC_CTL, (u8)((page << 2) | 0x01));
	if (ret < 0)
		return ret;
	ret = hh983_write_reg(client, SER_IND_ACC_ADDR, offset);
	if (ret < 0)
		return ret;
	return hh983_read_reg(client, SER_IND_ACC_DATA);
}

/* Read a deserializer indirect-page register (through 983 I2C passthrough). */
static int hh983_deser_ind_read(struct i2c_client *client, u8 deser_addr,
				u8 page, u8 offset)
{
	int ret;

	ret = hh983_write_deser_reg(client, deser_addr, SER_IND_ACC_CTL,
				    (u8)((page << 2) | 0x01));
	if (ret < 0)
		return ret;
	ret = hh983_write_deser_reg(client, deser_addr, SER_IND_ACC_ADDR, offset);
	if (ret < 0)
		return ret;
	return hh983_read_deser_reg(client, deser_addr, SER_IND_ACC_DATA);
}

/* Read the low byte of a deserializer APB register (DP TX block). */
static int hh983_deser_apb_read8(struct i2c_client *client, u8 deser_addr, u16 apb_addr)
{
	int ret;

	ret = hh983_write_deser_reg(client, deser_addr, SER_APB_CTL, APB_ENABLE);
	if (ret < 0)
		return ret;
	ret = hh983_write_deser_reg(client, deser_addr, SER_APB_ADR0, apb_addr & 0xFF);
	if (ret < 0)
		return ret;
	ret = hh983_write_deser_reg(client, deser_addr, SER_APB_ADR1, (apb_addr >> 8) & 0xFF);
	if (ret < 0)
		return ret;
	ret = hh983_write_deser_reg(client, deser_addr, SER_APB_CTL, APB_ENABLE | APB_READ);
	if (ret < 0)
		return ret;
	usleep_range(500, 1000);
	return hh983_read_deser_reg(client, deser_addr, SER_APB_DATA0);
}

/* Write a deserializer indirect-page register (through 983 I2C passthrough). */
static int hh983_deser_ind_write(struct i2c_client *client, u8 deser_addr,
				 u8 page, u8 offset, u8 value)
{
	int ret;

	ret = hh983_write_deser_reg(client, deser_addr, SER_IND_ACC_CTL, (u8)(page << 2));
	if (ret < 0)
		return ret;
	ret = hh983_write_deser_reg(client, deser_addr, SER_IND_ACC_ADDR, offset);
	if (ret < 0)
		return ret;
	return hh983_write_deser_reg(client, deser_addr, SER_IND_ACC_DATA, value);
}

/* Write a 32-bit deserializer APB register (DP TX block). */
static int hh983_deser_apb_write32(struct i2c_client *client, u8 deser_addr,
				   u16 apb_addr, u32 value)
{
	int ret;

	ret = hh983_write_deser_reg(client, deser_addr, SER_APB_CTL, APB_ENABLE);
	if (ret < 0)
		return ret;
	ret = hh983_write_deser_reg(client, deser_addr, SER_APB_ADR0, apb_addr & 0xFF);
	if (ret < 0)
		return ret;
	ret = hh983_write_deser_reg(client, deser_addr, SER_APB_ADR1, (apb_addr >> 8) & 0xFF);
	if (ret < 0)
		return ret;
	ret = hh983_write_deser_reg(client, deser_addr, SER_APB_DATA0, value & 0xFF);
	if (ret < 0)
		return ret;
	ret = hh983_write_deser_reg(client, deser_addr, SER_APB_DATA0 + 1, (value >> 8) & 0xFF);
	if (ret < 0)
		return ret;
	ret = hh983_write_deser_reg(client, deser_addr, SER_APB_DATA0 + 2, (value >> 16) & 0xFF);
	if (ret < 0)
		return ret;
	/* The write is issued when the last data byte is written */
	return hh983_write_deser_reg(client, deser_addr, SER_APB_DATA0 + 3, (value >> 24) & 0xFF);
}

/* Mode 0 DP guard: stop feeding the panel while the 983 has no input video. */
static void hh983_guard_cut_stream(struct hh983_data *data)
{
	struct i2c_client *client = data->client;

	if (hh983_deser_apb_write32(client, data->deser_addr,
				    DES984_APB_MAIN_STREAM_EN, 0) == 0)
		data->guard_stream_cut = true;
}

/* Read the 984's measured input line length and the 983's programmed output
 * line length, the pair the guard compares to decide whether the 984 DTG has
 * wedged.  Returns 0 with both filled in, or a negative value if any of the
 * four register reads failed.
 */
static int hh983_read_htotals(struct hh983_data *data, int *meas, int *prog)
{
	struct i2c_client *client = data->client;
	int meas_hi, meas_lo, prog_hi, prog_lo;

	meas_hi = hh983_deser_ind_read(client, data->deser_addr,
				       DES984_IND_PAGE_DTG, DES984_DTG_MEAS_HTOTAL_HI);
	meas_lo = hh983_deser_ind_read(client, data->deser_addr,
				       DES984_IND_PAGE_DTG, DES984_DTG_MEAS_HTOTAL_LO);
	prog_lo = hh983_ind_read(client, SER_IND_PAGE_VP, SER_VP0_H_TOTAL_LO);
	prog_hi = hh983_ind_read(client, SER_IND_PAGE_VP, SER_VP0_H_TOTAL_HI);
	if (meas_hi < 0 || meas_lo < 0 || prog_lo < 0 || prog_hi < 0)
		return -EIO;

	*meas = ((meas_hi & 0x7F) << 8) | meas_lo;
	*prog = (prog_hi << 8) | prog_lo;
	return 0;
}

/*
 * One-line context dump for the moment a wedge starts, for whoever ends up
 * scoping the DTG input clocking. The root cause is upstream of this driver --
 * a wedge that is not pulsed never recovers and takes the panel with it, which
 * is what the dtg_recover=0 experiment showed -- so the most useful thing the
 * driver can do is make sure the next one is not a mystery.
 *
 * Everything here is a register the driver already knows how to read, gathered
 * in the same poll that noticed the wedge. Emitted once per event: a wedge that
 * persists is re-reported every wedge_holdoff_s, and repeating this each time
 * would bury the transition that matters.
 *
 * Both paths that can detect a wedge call this -- the periodic check below and
 * the boot/resync restore -- gated on the same guard_wedged edge, because at
 * boot the restore path is the one that usually finds it.
 */
static void hh983_guard_wedge_snapshot(struct hh983_data *data, int meas, int prog)
{
	struct i2c_client *client = data->client;
	int mv_hi, mv_lo, meas_vtotal = -1;
	int vp_sts, ser_sts, stream_en, des_sts0, des_sts1;

	mv_hi = hh983_deser_ind_read(client, data->deser_addr,
				     DES984_IND_PAGE_DTG, DES984_DTG_MEAS_VTOTAL_HI);
	mv_lo = hh983_deser_ind_read(client, data->deser_addr,
				     DES984_IND_PAGE_DTG, DES984_DTG_MEAS_VTOTAL_LO);
	if (mv_hi >= 0 && mv_lo >= 0)
		meas_vtotal = ((mv_hi & 0x7F) << 8) | mv_lo;

	vp_sts    = hh983_ind_read(client, SER_IND_PAGE_VP, SER_VP0_STS);
	ser_sts   = hh983_read_reg(client, SER_GENERAL_STS);
	stream_en = hh983_deser_apb_read8(client, data->deser_addr,
					  DES984_APB_MAIN_STREAM_EN);
	des_sts0  = hh983_read_deser_reg(client, data->deser_addr, DES984_GP_STATUS_0);
	des_sts1  = hh983_read_deser_reg(client, data->deser_addr, DES984_GP_STATUS_1);

	dev_notice(&client->dev,
		   "DP guard wedge snapshot: 984 measured Htot=%d Vtot=%d, 983 programmed Htot=%d, "
		   "983 VP_STS=0x%02X GENERAL_STS=0x%02X, 984 stream_en=%d STS0=0x%02X STS1=0x%02X\n",
		   meas, meas_vtotal, prog, vp_sts, ser_sts, stream_en, des_sts0, des_sts1);
}

/* Mode 0 DP guard: bring the 984 output back after the 983 VP has resynced.
 *
 * Order matters and follows the sequence verified with a colorimeter:
 *   1. make sure the main stream is off while the DTG is touched;
 *   2. pulse the 984 DTG reset only if it is actually wedged (its measured
 *      input line length no longer matches the 983 programmed H total);
 *   3. wait for the DTG to settle, then enable the main stream.
 * On a healthy pipeline (stream on, DTG fine) this is a no-op, so displays
 * that never lost video are not disturbed.
 *
 * force_wedged is for a caller that has already measured and decided -- the
 * DTG-wedge check below. The measurement jitters by a pixel or two from one
 * read to the next, so re-deciding here would let the recovery be skipped for
 * a caller that had just seen a bad value, and would make the log claim a
 * restore that never happened.
 *
 * When the measurement cannot be read at all this used to assume a wedge and
 * pulse anyway, on the reasoning that a pulse is harmless with the stream off.
 * It is not harmless: on 2026-09-10 a pulse on a healthy DTG black-latched the
 * OTS-OLED panel, recoverable only by a power cycle. The read is retried once
 * and then the stream is simply re-enabled without a pulse -- and if the DTG
 * really was wedged, the periodic check catches it within two polls and pulses
 * then, on a measurement it actually has.
 */
static void hh983_guard_restore_stream(struct hh983_data *data, bool force_wedged)
{
	struct i2c_client *client = data->client;
	int stream_en;
	int meas_htotal = -1, prog_htotal = -1;
	bool wedged = force_wedged;
	bool unreadable = false;

	stream_en = hh983_deser_apb_read8(client, data->deser_addr,
					  DES984_APB_MAIN_STREAM_EN);

	if (hh983_read_htotals(data, &meas_htotal, &prog_htotal) != 0) {
		/* One retry: a single failed transfer on a bus this busy is not
		 * evidence of anything. */
		msleep(20);
		unreadable = hh983_read_htotals(data, &meas_htotal, &prog_htotal) != 0;
	}

	if (!force_wedged && !unreadable)
		wedged = abs(meas_htotal - prog_htotal) > dtg_tolerance;

	if (unreadable && !force_wedged) {
		dev_info(&client->dev,
			 "DP guard restore: H totals unreadable, enabling stream without DTG pulse\n");
	} else {
		dev_info(&client->dev,
			 "DP guard restore: 984 stream_en=%d, DTG measured Htotal=%d, 983 Htotal=%d%s\n",
			 stream_en, meas_htotal, prog_htotal, wedged ? " (DTG wedged)" : "");
	}

	/* Boot and resync catch most wedges, so the snapshot has to be here too or
	 * it would rarely fire.  Same guard_wedged edge the periodic check uses, so
	 * one wedge never prints two snapshots: when that check calls us with
	 * force_wedged it has already snapshotted and set the flag. */
	if (wedged && !data->guard_wedged) {
		data->guard_wedged = true;
		hh983_guard_wedge_snapshot(data, meas_htotal, prog_htotal);
	}

	if (!wedged && !unreadable && stream_en == 1 && !data->guard_stream_cut)
		return;	/* healthy, nothing to do */

	if (stream_en != 0)
		hh983_deser_apb_write32(client, data->deser_addr,
					DES984_APB_MAIN_STREAM_EN, 0);

	if (wedged) {
		hh983_deser_ind_write(client, data->deser_addr, DES984_IND_PAGE_DTG,
				      DES984_DTG_P0_CTL, DES984_DTG_HOLD_RESET);
		hh983_deser_ind_write(client, data->deser_addr, DES984_IND_PAGE_DTG,
				      DES984_DTG_P1_CTL, DES984_DTG_HOLD_RESET);
		msleep(200);
		hh983_deser_ind_write(client, data->deser_addr, DES984_IND_PAGE_DTG,
				      DES984_DTG_P0_CTL, DES984_DTG_RELEASE);
		hh983_deser_ind_write(client, data->deser_addr, DES984_IND_PAGE_DTG,
				      DES984_DTG_P1_CTL, DES984_DTG_RELEASE);
	}
	msleep(500);

	if (hh983_deser_apb_write32(client, data->deser_addr,
				    DES984_APB_MAIN_STREAM_EN, 1) == 0)
		data->guard_stream_cut = false;

	msleep(50);
	stream_en = hh983_deser_apb_read8(client, data->deser_addr,
					  DES984_APB_MAIN_STREAM_EN);
	if (stream_en != 1)
		dev_warn(&client->dev,
			 "DP guard restore: 984 main stream readback %d (expected 1)\n",
			 stream_en);
}

/* Mode 0 DP guard, second failure mode: the 984 DTG's measured line length
 * wanders off while the 983 stays synced and the main stream stays on.  The
 * VP-sync guard never sees this -- there is no video loss to trigger it -- and
 * the panel latches black about ten seconds later, recoverable only by a power
 * cycle.  Both black screens recorded on 2026-09-10 had this signature (once
 * with a measured H total of 5313 against a programmed 3440).
 *
 * Runs on every poll while video is up, and hands the recovery to the same
 * restore path a resync uses, so there is exactly one place that knows the safe
 * order.  Two consecutive bad polls plus the restore is about 3 s, inside the
 * panel's ~5 s tolerance for distorted timing.
 */
static void hh983_guard_check_dtg(struct hh983_data *data)
{
	struct i2c_client *client = data->client;
	int meas = -1, prog = -1;

	if (!dtg_check || data->guard_stream_cut)
		return;

	if (hh983_read_htotals(data, &meas, &prog) < 0) {
		/* A failed read is not evidence of a wedge -- a bus that has
		 * gone away is the VP-sync guard's problem, not this one. */
		data->guard_dtg_count = 0;
		return;
	}

	if (abs(meas - prog) <= dtg_tolerance) {
		data->guard_dtg_count = 0;
		data->guard_wedged = false;
		return;
	}

	data->guard_dtg_count++;
	if (data->guard_dtg_count < DP_GUARD_WEDGE_POLLS)
		return;			/* one odd measurement is not a wedge */
	data->guard_dtg_count = 0;

	/* Before the holdoff, so a new wedge always gets its context even when the
	 * pulse itself is held off after a recent one. */
	if (!data->guard_wedged) {
		data->guard_wedged = true;
		hh983_guard_wedge_snapshot(data, meas, prog);
	}

	/* A link that is genuinely broken would otherwise loop the DTG pulse. */
	if (data->guard_wedge_armed &&
	    time_before(jiffies, data->guard_wedge_at +
				 msecs_to_jiffies(wedge_holdoff_s * 1000)))
		return;

	data->guard_wedge_at = jiffies;
	data->guard_wedge_armed = true;
	dtg_wedge_count++;

	if (!dtg_recover) {
		dev_notice(&client->dev,
			   "DP guard: DTG wedge detected (measured %d, programmed %d), NOT recovering (log-only)\n",
			   meas, prog);
		return;
	}

	dev_notice(&client->dev,
		   "DP guard: DTG wedge without video loss (measured %d, programmed %d), restoring\n",
		   meas, prog);
	hh983_guard_restore_stream(data, true);
}

/* poll_interval_ms sysfs write: the poll work only reschedules itself while
 * the interval is positive, so a 0 stops it for good.  Restart it here when
 * a positive value is written and the work is not pending.
 */
static int hh983_set_poll_interval(const char *val, const struct kernel_param *kp)
{
	int ret = param_set_int(val, kp);

	if (ret)
		return ret;
	if (poll_interval_ms > 0 && hh983_poll_owner &&
	    !delayed_work_pending(&hh983_poll_owner->link_work))
		schedule_delayed_work(&hh983_poll_owner->link_work,
				      msecs_to_jiffies(poll_interval_ms));
	return 0;
}

/* Mode 0 DP guard poll.  Tracks the 983 VP0 timing-generator sync bit:
 * lost for DP_GUARD_LOSS_POLLS -> cut the 984 stream (must happen within a
 * few seconds; the OTS-OLED panel tolerates ~5 s of distorted timing but
 * latches black at ~10 s), back for DP_GUARD_RESYNC_POLLS -> restore.
 */
static void hh983_dp_guard_work_fn(struct work_struct *work)
{
	struct hh983_data *data = container_of(work, struct hh983_data,
					       link_work.work);
	struct i2c_client *client = data->client;
	int vp_sts;
	bool synced;

	vp_sts = hh983_ind_read(client, SER_IND_PAGE_VP, SER_VP0_STS);
	if (vp_sts < 0)
		goto resched;
	synced = (vp_sts & 0x01) != 0;

	if (data->guard_video_up) {
		if (!synced) {
			data->down_count++;
			if (data->down_count >= DP_GUARD_LOSS_POLLS) {
				dev_notice(&client->dev,
					   "DP video lost (VP_STS=0x%02X), cutting 984 video stream\n",
					   vp_sts);
				hh983_guard_cut_stream(data);
				data->guard_video_up = false;
				data->guard_up_count = 0;
				data->guard_dtg_count = 0;
				data->guard_wedged = false;
			}
		} else {
			data->down_count = 0;
			/* Video is up and the stream is on: the only failure
			 * left to look for is the DTG wedging under us. */
			hh983_guard_check_dtg(data);
		}
	} else {
		if (synced) {
			data->guard_up_count++;
			if (data->guard_up_count >= DP_GUARD_RESYNC_POLLS) {
				dev_notice(&client->dev,
					   "DP video back (VP_STS=0x%02X), restoring 984 video stream\n",
					   vp_sts);
				data->guard_wedged = false;
				hh983_guard_restore_stream(data, false);
				data->guard_video_up = true;
				data->down_count = 0;
				data->guard_dtg_count = 0;
				data->recovery_count++;
			}
		} else {
			data->guard_up_count = 0;
		}
	}

resched:
	if (poll_interval_ms > 0)
		schedule_delayed_work(&data->link_work,
				      msecs_to_jiffies(poll_interval_ms));
}

/* Reset the video link: GPIO toggle + digital reset + HPD toggle.
 * Reusable by both probe() and the link monitor work function.
 *
 * Modes 0 and 1 only: mode 2 starts no link monitor, so nothing calls this.
 * If that ever changes, note that the sequence below does a 983 digital reset
 * and an HPD toggle unconditionally, which a mode-2 board must not get for
 * free -- plan 4.3 describes the mode-2 variant (983 reset + HPD only, no 988
 * GPIO forcing) if phase 2 is ever needed.
 *
 * Sequence (both mode 0/984 and mode 1/988):
 *   1. Ensure I2C passthrough is up (so we can talk to deserializer)
 *   2. Deserializer GPIO4/GPIO6 LOW — hold display driver board in reset
 *   3. 983 digital reset — clear stale FPDLink TX + DP RX pipelines
 *   4. Deserializer GPIO4/GPIO6 restore — un-reset display driver board
 *   5. Re-enable I2C passthrough (safety measure)
 *   6. HPD toggle — force DP source to re-read EDID and re-train
 */
static void hh983_recover_link(struct hh983_data *data)
{
	struct i2c_client *client = data->client;

	dev_info(&client->dev, "Recovering video link (recovery #%d)...\n",
		 data->recovery_count + 1);

	if (data->mode == 1) {
		/* Ensure I2C passthrough works so we can reach the 988.
		 * FPDLink control channel stays up even when DP input is lost,
		 * so this write goes through even with a black display.
		 */
		hh983_write_reg(client, SER_I2C_CONTROL, SER_ENABLE_PASSTHROUGH);
		msleep(10);

		/* Force GPIO4/GPIO6 LOW — resets display driver board.
		 * These pins drive LOCK0/LOCK_DUAL which are reset signals
		 * on the display driver board's internal circuitry.
		 */
		hh983_write_deser_reg(client, data->deser_addr,
				      DES988_GPIO4_PIN_CTL, DES988_GPIO_FORCED_LOW);
		hh983_write_deser_reg(client, data->deser_addr,
				      DES988_GPIO6_PIN_CTL, DES988_GPIO_FORCED_LOW);
	} else if (data->mode == 0) {
		/* Same GPIO reset sequence for 984 — registers and values
		 * are identical to 988 (GPIO4=0x19, GPIO6=0x1B, LOW=0xC0).
		 */
		hh983_write_reg(client, SER_I2C_CONTROL, SER_ENABLE_PASSTHROUGH);
		msleep(10);

		hh983_write_deser_reg(client, data->deser_addr,
				      DES984_GPIO4_PIN_CTL, DES984_GPIO_FORCED_LOW);
		hh983_write_deser_reg(client, data->deser_addr,
				      DES984_GPIO6_PIN_CTL, DES984_GPIO_FORCED_LOW);
	}

	/* Digital reset: resets FPDLink TX + DP RX pipelines.
	 * Self-clearing bit, preserves register configuration.
	 * Display driver held in reset during this period (GPIOs still LOW).
	 */
	hh983_write_reg(client, SER_RESET_CTL, SER_DIGITAL_RESET_0);
	msleep(500);

	if (data->mode == 1) {
		/* Restore GPIOs to lock-indicator mode — un-resets display driver */
		hh983_write_deser_reg(client, data->deser_addr,
				      DES988_GPIO4_PIN_CTL, DES988_GPIO4_RX_LOCK);
		hh983_write_deser_reg(client, data->deser_addr,
				      DES988_GPIO6_PIN_CTL, DES988_GPIO6_COMBINED_LOCK);
		msleep(300);
	} else if (data->mode == 0) {
		hh983_write_deser_reg(client, data->deser_addr,
				      DES984_GPIO4_PIN_CTL, DES984_GPIO4_RX_LOCK);
		hh983_write_deser_reg(client, data->deser_addr,
				      DES984_GPIO6_PIN_CTL, DES984_GPIO6_COMBINED_LOCK);
		msleep(300);
	}

	/* Re-enable I2C passthrough as safety measure.
	 * Digital reset preserves regs, but re-enabling ensures clean state.
	 */
	hh983_write_reg(client, SER_I2C_CONTROL, SER_ENABLE_PASSTHROUGH);
	usleep_range(1000, 2000);
	if (data->mode == 1) {
		hh983_write_deser_reg(client, data->deser_addr,
				      DES988_I2C_CONTROL, DES988_ENABLE_PASSTHROUGH);
		usleep_range(1000, 2000);
	} else if (data->mode == 0) {
		hh983_write_deser_reg(client, data->deser_addr,
				      DES984_GENERAL_CFG, DES984_ENABLE_PASSTHROUGH);
		usleep_range(1000, 2000);
	}

	/* HPD toggle: force DP source to re-read EDID and re-train */
	hh983_apb_write(client, APB_LINK_ENABLE, 0x00);
	msleep(200);
	hh983_apb_write(client, APB_LINK_ENABLE, 0x01);
	msleep(500);

	hh983_check_link_status(data);
	data->recovery_count++;
}

/* Clear pending DP RX events by reading the read-clear cause register. */
static void hh983_clear_dp_events(struct hh983_data *data)
{
	hh983_apb_read(data->client, APB_SINK_0_INT_CAUSE);
}

/* Periodic link status monitor — detects DP input video loss/return
 * and triggers automatic video recovery.
 *
 * Detection method: APB_SINK_0_INT_CAUSE (0x194) — fires on video events
 * (NO_VIDEO, VIDEO_DETECT, VIDEO_MODE_CHANGE). This is the only register
 * empirically confirmed to change when the DP input is interrupted.
 *
 * Registers that do NOT work for detection:
 *   - GENERAL_STS (0x0C): reflects FPDLink back-channel, not DP input
 *   - APB_PHY_STATUS (0x208): latched, never clears on input loss
 *   - APB_INTERRUPT_CAUSE (0x188): stuck at 0xFFFF, mask (0x180) not writable
 *
 * Fallback: blind recovery after 10+ poll cycles with no events.
 */
static void hh983_link_work_fn(struct work_struct *work)
{
	struct hh983_data *data = container_of(work, struct hh983_data,
					       link_work.work);
	struct i2c_client *client = data->client;
	int sink_cause;

	/* Skip polling during post-recovery cooldown */
	if (data->recovery_cooldown > 0) {
		data->recovery_cooldown--;
		goto resched;
	}

	/* Read SINK_0_INTERRUPT_CAUSE — read-clear register.
	 * Any non-zero value means a video event occurred.
	 */
	sink_cause = hh983_apb_read(client, APB_SINK_0_INT_CAUSE);
	if (sink_cause < 0)
		goto resched;

	if (sink_cause) {
		dev_info(&client->dev,
			 "Video event: SINK_INT=0x%02X [%s%s%s]\n",
			 sink_cause,
			 (sink_cause & 0x04) ? "NO_VIDEO " : "",
			 (sink_cause & 0x02) ? "VIDEO_DETECT " : "",
			 (sink_cause & 0x01) ? "MODE_CHANGE " : "");

		if (sink_cause & 0x04) {
			/* NO_VIDEO: DP source stopped sending video */
			data->link_up = false;
			data->down_count = 0;
		}

		if ((sink_cause & 0x02) || (sink_cause & 0x01)) {
			/* VIDEO_DETECT or MODE_CHANGE: video (re)appeared.
			 * The 983 saw new video but the pipeline may be stale
			 * from the interruption — recovery re-trains everything.
			 */
			dev_notice(&client->dev,
				   "Video detected, triggering recovery\n");
			hh983_recover_link(data);
			data->link_up = true;
			data->down_count = 0;
			data->recovery_cooldown = 5;
			goto resched;
		}

		/* Upper bits (0xF0 seen empirically) may indicate
		 * undocumented video events — treat as recovery trigger
		 * if we're currently down or if no specific bit matched.
		 */
		if (!data->link_up || !(sink_cause & 0x07)) {
			dev_notice(&client->dev,
				   "Video event (0x%02X) while %s, recovering\n",
				   sink_cause,
				   data->link_up ? "up" : "down");
			hh983_recover_link(data);
			data->link_up = true;
			data->down_count = 0;
			data->recovery_cooldown = 5;
			goto resched;
		}
	}

	/* Blind recovery fallback: if link has been down for 10+ polls
	 * with no SINK events, try recovery anyway. Harmless if cable
	 * is still out (no source to train with).
	 */
	if (!data->link_up) {
		data->down_count++;
		if (data->down_count >= 10) {
			dev_notice(&client->dev,
				   "Link down for %ds, blind recovery\n",
				   data->down_count * poll_interval_ms / 1000);
			hh983_recover_link(data);
			data->down_count = 0;
			data->recovery_cooldown = 5;
		}
	}

resched:
	if (poll_interval_ms > 0)
		schedule_delayed_work(&data->link_work,
				      msecs_to_jiffies(poll_interval_ms));
}

/* Configure REM_INTB on serializer (common to both modes) */
static int hh983_configure_rem_intb(struct i2c_client *client, int port, int mode)
{
	int ret, readback;
	u8 gpio4_val = (port == 0) ? SER_GPIO4_PORT0_REM_INT : SER_GPIO4_PORT1_REM_INT;
	u8 int_val = (mode == 0) ? SER_ENABLE_GLOBAL_INT_NO_DP : SER_ENABLE_GLOBAL_INT;

	dev_info(&client->dev, "Configuring REM_INTB for Port %d (GPIO4=0x%02X, INT_CTL=0x%02X)\n",
		 port, gpio4_val, int_val);

	/* Enable REM_INT in interrupt control */
	ret = hh983_write_reg(client, SER_INTERRUPT_CTRL, SER_ENABLE_REM_INT);
	if (ret < 0)
		return ret;
	usleep_range(1000, 2000);

	/* Configure GPIO4 for REM_INT forwarding */
	dev_info(&client->dev, "Writing GPIO4_CONFIG (0x%02X) = 0x%02X\n", SER_GPIO4_CONFIG, gpio4_val);
	ret = hh983_write_reg(client, SER_GPIO4_CONFIG, gpio4_val);
	if (ret < 0)
		return ret;
	usleep_range(1000, 2000);

	/* Verify write */
	readback = hh983_read_reg(client, SER_GPIO4_CONFIG);
	if (readback != gpio4_val)
		dev_warn(&client->dev, "GPIO4_CONFIG readback mismatch: wrote 0x%02X, read 0x%02X\n",
			 gpio4_val, readback);
	else
		dev_info(&client->dev, "GPIO4_CONFIG verified: 0x%02X\n", readback);

	/* Enable global INTB — mode 0 (984) omits IE_DP_RX0 to keep
	 * DP video interrupts off the shared INTB/GPIO4 line, avoiding
	 * interference with touch REM_INTB.  The link monitor polls
	 * APB_SINK_0_INT_CAUSE directly so INTB is not needed for DP.
	 */
	ret = hh983_write_reg(client, SER_INTERRUPT_CTL, int_val);
	if (ret < 0)
		return ret;

	return 0;
}

/* Mode 0: 983 + 984 configuration */
static int hh983_init_mode_984(struct hh983_data *data)
{
	struct i2c_client *client = data->client;
	int ret;

	dev_info(&client->dev, "Initializing Mode 0: 983 + 984\n");

	/* Enable I2C passthrough on serializer */
	ret = hh983_write_reg(client, SER_I2C_CONTROL, SER_ENABLE_PASSTHROUGH);
	if (ret < 0)
		return ret;
	msleep(10);

	/* Configure REM_INTB for Port 0 (no DP RX interrupt for 984) */
	ret = hh983_configure_rem_intb(client, 0, 0);
	if (ret < 0)
		return ret;

	/* Enable INTB on 984 deserializer.
	 * Clear first to reset any latched interrupt from a previous session.
	 */
	hh983_write_deser_reg(client, data->deser_addr, DES984_INTB_ENABLE, 0x00);
	usleep_range(2000, 3000);

	ret = hh983_write_deser_reg(client, data->deser_addr, DES984_INTB_ENABLE, DES984_INTB_VALUE);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to configure 984 INTB\n");
		return ret;
	}

	if (ots_touch) {
		/* Route HX8530 (phys 0x49 on 984 I2C Port 1) to host 0x48.
		 * TARGET_ID  = physical remote address, TARGET_ALIAS = host address,
		 * TARGET_DEST = 0x20 (deserializer I2C Port 1).  Pass-through for the
		 * other devices (0x2c/0x50/0x66/0x70 on Port 0) stays as set above.
		 */
		ret = hh983_write_reg(client, SER_TARGET_ID0,
				      MAKE_TARGET_ID(OTS_TOUCH_PHYS_ADDR));
		if (ret < 0)
			return ret;
		ret = hh983_write_reg(client, SER_TARGET_ALIAS0,
				      MAKE_TARGET_ID(OTS_TOUCH_HOST_ADDR));
		if (ret < 0)
			return ret;
		ret = hh983_write_reg(client, SER_TARGET_DEST0, TARGET_DEST_PORT1);
		if (ret < 0)
			return ret;
		dev_info(&client->dev,
			 "OTS touch routed: host 0x%02x -> 984 Port 1 phys 0x%02x\n",
			 OTS_TOUCH_HOST_ADDR, OTS_TOUCH_PHYS_ADDR);
	}

	dev_info(&client->dev, "Mode 0 (983+984) initialization complete\n");
	return 0;
}

/* Mode 1: 983 + 988 configuration */
static int hh983_init_mode_988(struct hh983_data *data)
{
	struct i2c_client *client = data->client;
	int ret;

	dev_info(&client->dev, "Initializing Mode 1: 983 + 988 (TDDI passthrough)\n");

	/* Step 1: Enable I2C passthrough on serializer */
	ret = hh983_write_reg(client, SER_I2C_CONTROL, SER_ENABLE_PASSTHROUGH);
	if (ret < 0)
		return ret;
	msleep(10);

	/* Step 2: Enable I2C passthrough on 988 deserializer */
	ret = hh983_write_deser_reg(client, data->deser_addr, DES988_I2C_CONTROL, DES988_ENABLE_PASSTHROUGH);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to enable 988 passthrough\n");
		return ret;
	}
	usleep_range(5000, 10000);

	/* Step 3: Check link status */
	ret = hh983_read_deser_reg(client, data->deser_addr, DES988_GP_STATUS_0);
	if (ret >= 0)
		dev_info(&client->dev, "988 RX Lock Status: 0x%02X\n", ret);

	/* Step 4: Configure TARGET_ID/ALIAS/DEST for TDDI 0x48 -> Port 1 */
	ret = hh983_write_reg(client, SER_TARGET_ID0, MAKE_TARGET_ID(TDDI_ADDR_1));
	if (ret < 0)
		return ret;
	ret = hh983_write_reg(client, SER_TARGET_ALIAS0, MAKE_TARGET_ID(TDDI_ADDR_1));
	if (ret < 0)
		return ret;
	ret = hh983_write_reg(client, SER_TARGET_DEST0, TARGET_DEST_PORT1);
	if (ret < 0)
		return ret;

	/* Step 5: Configure TARGET_ID/ALIAS/DEST for TDDI 0x49 -> Port 1 */
	ret = hh983_write_reg(client, SER_TARGET_ID1, MAKE_TARGET_ID(TDDI_ADDR_2));
	if (ret < 0)
		return ret;
	ret = hh983_write_reg(client, SER_TARGET_ALIAS1, MAKE_TARGET_ID(TDDI_ADDR_2));
	if (ret < 0)
		return ret;
	ret = hh983_write_reg(client, SER_TARGET_DEST1, TARGET_DEST_PORT1);
	if (ret < 0)
		return ret;

	/* Step 6: Configure REM_INTB for Port 0
	 * IMPORTANT: Use Port 0, NOT Port 1!
	 * - TARGET_DEST_PORT1 routes I2C to the 988's I2C Port 1 (where TDDI is connected)
	 * - But REM_INT comes over the single FPDLink link, which is always Port 0
	 * - The 983 has only one deserializer (988) connected = FPDLink Port 0
	 * Signal path: TDDI touch_int -> 988 INTB_IN -> BCC Port 0 -> 983 REM_INTB -> GPIO4
	 */
	ret = hh983_configure_rem_intb(client, 0, 1);
	if (ret < 0)
		return ret;

	/* Step 7: Enable INTB_IN forwarding on 988 deserializer (datasheet 7.3.9)
	 * RX_INTN_CTL (0x44) bit 7 = 1 enables INTB_IN -> back channel -> REM_INTB
	 * Signal path: TDDI touch_int -> 988 INTB_IN (pin 45) -> BCC -> 983 REM_INTB -> Host GPIO
	 *
	 * First ensure INTB_IN is disabled to clear any latched interrupt state
	 * from a previous session. Without this, a stale interrupt can latch
	 * GPIO4 LOW immediately on enable, causing an All Zero flood in the
	 * touch driver.
	 */
	hh983_write_deser_reg(client, data->deser_addr, DES988_RX_INTN_CTL, 0x00);
	usleep_range(2000, 3000);

	ret = hh983_write_deser_reg(client, data->deser_addr, DES988_RX_INTN_CTL, DES988_INTB_IN_ENABLE);
	if (ret < 0) {
		dev_warn(&client->dev, "Failed to configure 988 INTB_IN forwarding\n");
		/* Don't fail - passthrough may still work */
	}

	dev_info(&client->dev, "Mode 1 (983+988) initialization complete\n");
	dev_info(&client->dev, "TDDI 0x%02X and 0x%02X should be visible on I2C bus\n",
		 TDDI_ADDR_1, TDDI_ADDR_2);

	/* Allow FPDLink I2C passthrough to fully stabilize before returning.
	 * The himax touch driver may probe during this delay via deferred probe.
	 * Initial I2C commands work but touch reporting can be degraded if the
	 * link isn't fully stable. 100ms provides adequate stabilization.
	 */
	msleep(100);

	return 0;
}

/* Mode 2: 983 + 988, video only (no touch controller on the panel).
 *
 * Deliberately does far less than mode 1, and the omissions are the point:
 *
 *   - No SER_TARGET_ID0/ALIAS0/DEST0 writes.  On a 3x QVue the RH850 firmware
 *     has already used target slot 0 to alias the 988 from its strapped
 *     physical 7-bit 0x38 to 0x2C (TARGET_ID0=0x70, TARGET_ALIAS0=0x58).
 *     Overwriting that slot -- which mode 1 does, with 0x90/0x90/0x20 for the
 *     TDDI touch -- would make the 988 vanish from 0x2C for every later
 *     access: link status here, the recovery path, and every Pi-side debug
 *     script.  On the existing 988 boards this went unnoticed because their
 *     988 sits at 0x2C physically, so plain pass-through still reached it.
 *
 *   - No hh983_configure_rem_intb() and no DES988_RX_INTN_CTL.  There is no
 *     touch controller, so the 988's INTB_IN pin floats and REM_INTB toward
 *     the host GPIO has no consumer; arming an interrupt path with no owner
 *     is a good way to latch GPIO4 LOW for no reason.
 *
 * What is left is the pass-through pair plus a lock-status read, which is
 * everything a video-only board needs from the host.
 */
static int hh983_init_mode_988_video(struct hh983_data *data)
{
	struct i2c_client *client = data->client;
	int ret;

	dev_info(&client->dev, "Initializing Mode 2: 983 + 988 (video only, no touch)\n");

	/* Step 1: Enable I2C passthrough on serializer */
	ret = hh983_write_reg(client, SER_I2C_CONTROL, SER_ENABLE_PASSTHROUGH);
	if (ret < 0)
		return ret;
	msleep(10);

	/* Step 2: Enable I2C passthrough on 988 deserializer */
	ret = hh983_write_deser_reg(client, data->deser_addr, DES988_I2C_CONTROL,
				    DES988_ENABLE_PASSTHROUGH);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to enable 988 passthrough\n");
		return ret;
	}
	usleep_range(5000, 10000);

	/* Step 3: Report link status.  Both registers, because a video-only
	 * board gives no other sign of life -- there is no touch device
	 * appearing on the bus to confirm the back channel works.
	 */
	ret = hh983_read_deser_reg(client, data->deser_addr, DES988_GP_STATUS_0);
	if (ret >= 0)
		dev_info(&client->dev, "988 GP_STATUS_0=0x%02X [%s%s]\n", ret,
			 (ret & 0x01) ? "FPD4_LOCK " : "",
			 (ret & 0x04) ? "FPDTX_PLL_LOCK" : "");

	ret = hh983_read_deser_reg(client, data->deser_addr, DES988_GP_STATUS_1);
	if (ret >= 0)
		dev_info(&client->dev, "988 GP_STATUS_1=0x%02X [%s%s%s]\n", ret,
			 (ret & 0x02) ? "SIG_DET " : "",
			 (ret & 0x40) ? "FPD_PLL_LOCK " : "",
			 (ret & 0x01) ? "LOCK" : "NO_LOCK");

	dev_info(&client->dev, "Mode 2 (983+988 video only) initialization complete\n");
	return 0;
}

/* Kernel 6.3+ changed I2C probe signature - handle both versions */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
static int hh983_probe(struct i2c_client *client)
#else
static int hh983_probe(struct i2c_client *client, const struct i2c_device_id *id)
#endif
{
	struct hh983_data *data;
	int ret;

	dev_info(&client->dev, "HH983 FPDLink serializer probe (config_mode=%d)\n", config_mode);

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	data->deser_addr = 0x2C;  /* Default deserializer address */
	data->mode = config_mode;
	i2c_set_clientdata(client, data);

	/* Skip probe-time recovery for both modes.
	 * The HPD toggle + digital reset in hh983_recover_link() tears down
	 * the DP link that vc4-kms-v3d has already established during boot,
	 * causing the display to go black seconds after boot messages appear.
	 * Recovery should only be triggered by the link monitor on actual
	 * video loss events, not unconditionally at probe.
	 */

	switch (data->mode) {
	case 0:
		ret = hh983_init_mode_984(data);
		break;
	case 1:
		ret = hh983_init_mode_988(data);
		break;
	case 2:
		ret = hh983_init_mode_988_video(data);
		break;
	default:
		dev_err(&client->dev, "Invalid config_mode %d (use 0, 1 or 2)\n", data->mode);
		return -EINVAL;
	}

	if (ret < 0) {
		dev_err(&client->dev, "Initialization failed: %d\n", ret);
		return ret;
	}

	data->initialized = true;
	hh983_check_link_status(data);

	/* Link monitoring and APB interrupt unmasking are only needed for
	 * mode 1 (988) where HDMI-switch recovery is supported.  Mode 0
	 * (984) matches the old driver behavior: configure registers and
	 * leave the existing DP link undisturbed.  Mode 2 does neither in
	 * phase 1: no delayed work is started and APB_SINK_0_INT_MASK is
	 * left at its default, so poll_interval_ms has no effect there.
	 * Whether a mode-2 monitor is needed is decided from bench evidence
	 * (does the QVue come back on its own after a Pi reboot?), not up
	 * front -- see plan 4.3.
	 */
	if (data->mode == 1) {
		/* Unmask SINK_0 video interrupts so SINK_0_INT_CAUSE fires on
		 * NO_VIDEO / VIDEO_DETECT / VIDEO_MODE_CHANGE events.
		 * Default mask is 0x79 (most events masked).
		 */
		hh983_apb_write(client, APB_SINK_0_INT_MASK, 0x00);

		/* Clear any DP events from boot training before starting monitor */
		hh983_clear_dp_events(data);

		/* Start link monitoring */
		data->link_up = true;
		INIT_DELAYED_WORK(&data->link_work, hh983_link_work_fn);
		hh983_poll_owner = data;
		if (poll_interval_ms > 0)
			schedule_delayed_work(&data->link_work,
					      msecs_to_jiffies(poll_interval_ms));
	} else if (data->mode == 0 && dp_guard) {
		/* Start in the "down" state: after a reboot the shutdown hook
		 * has left the 984 stream cut and the 984 DTG is usually wedged
		 * by the outage, so the first stable VP sync triggers a restore
		 * (DTG pulse + stream enable).  The existing DP link is not
		 * touched.
		 */
		data->guard_video_up = false;
		data->guard_up_count = 0;
		data->guard_dtg_count = 0;
		data->guard_wedged = false;
		data->guard_wedge_armed = false;
		INIT_DELAYED_WORK(&data->link_work, hh983_dp_guard_work_fn);
		hh983_poll_owner = data;
		if (poll_interval_ms > 0)
			schedule_delayed_work(&data->link_work,
					      msecs_to_jiffies(poll_interval_ms));
		if (poll_interval_ms > 2000)
			dev_warn(&client->dev,
				 "dp_guard needs poll_interval_ms <= 2000 to cut the stream within the panel tolerance (~5 s)\n");
	}

	dev_info(&client->dev, "HH983 initialization successful (mode=%d, poll=%s%s%s)\n",
		 data->mode,
		 (poll_interval_ms > 0 && (data->mode == 1 || (data->mode == 0 && dp_guard))) ? "on" : "off",
		 (data->mode == 0 && dp_guard) ? ", dp_guard" : "",
		 (data->mode == 0 && dp_guard && dtg_check)
			? (dtg_recover ? ", dtg_check" : ", dtg_check(log-only)") : "");
	return 0;
}

/* System shutdown/reboot: DP video is about to disappear for tens of seconds.
 * Cut the 984 video stream now so the eDP panel idles instead of latching
 * black on the distorted timing; probe() restores it after the reboot.
 */
static void hh983_shutdown(struct i2c_client *client)
{
	struct hh983_data *data = i2c_get_clientdata(client);

	if (!data || !data->initialized)
		return;

	if (data->mode == 0 && dp_guard) {
		cancel_delayed_work_sync(&data->link_work);
		dev_info(&client->dev, "shutdown: cutting 984 video stream\n");
		hh983_guard_cut_stream(data);
	}
}

static void hh983_remove(struct i2c_client *client)
{
	struct hh983_data *data = i2c_get_clientdata(client);

	dev_info(&client->dev, "HH983 driver removed\n");

	if (data && data->initialized) {
		/* Stop link monitor before tearing down hardware
		 * (only initialized for mode 1 and for mode 0 with dp_guard)
		 */
		if (data->mode == 1 || (data->mode == 0 && dp_guard)) {
			hh983_poll_owner = NULL;
			cancel_delayed_work_sync(&data->link_work);
		}

		/* Mode 2 armed no interrupt chain and owns no reset: it only
		 * enabled pass-through.  Tearing down what was never set up
		 * would be noise, and the digital reset below would drop a
		 * running video link on a plain module reload, so mode 2 stops
		 * here.
		 */
		if (data->mode == 2) {
			dev_info(&client->dev,
				 "Mode 2: nothing to tear down, link left running\n");
			return;
		}

		/* Tear down the full interrupt chain in reverse order.
		 * Just disabling GPIO4 leaves REM_INT and INTB_IN active,
		 * which can latch an interrupt that persists across
		 * rmmod/modprobe and holds GPIO4 stuck LOW on next probe.
		 */
		if (data->mode == 1) {
			/* Disable 988 INTB_IN forwarding first (source end) */
			hh983_write_deser_reg(client, data->deser_addr,
					      DES988_RX_INTN_CTL, 0x00);
			usleep_range(2000, 3000);
		} else if (data->mode == 0) {
			/* Disable 984 INTB forwarding */
			hh983_write_deser_reg(client, data->deser_addr,
					      DES984_INTB_ENABLE, 0x00);
			usleep_range(2000, 3000);
		}

		/* Disable global INTB output */
		hh983_write_reg(client, SER_INTERRUPT_CTL, 0x00);

		/* Disable REM_INT */
		hh983_write_reg(client, SER_INTERRUPT_CTRL, 0x00);

		/* Disable GPIO4 REM_INT forwarding */
		hh983_write_reg(client, SER_GPIO4_CONFIG, 0x00);

		/* Digital reset to force a clean link state.
		 * On next probe, another reset + HPD toggle will
		 * re-establish both FPDLink and DP paths.
		 */
		hh983_write_reg(client, SER_RESET_CTL, SER_DIGITAL_RESET_0);
		dev_info(&client->dev, "Digital reset issued for clean state on next probe\n");
	}
}

static const struct i2c_device_id hh983_id[] = {
	{ "hh983-serializer", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, hh983_id);

static const struct of_device_id hh983_of_match[] = {
	{ .compatible = "ti,hh983-serializer" },
	{ }
};
MODULE_DEVICE_TABLE(of, hh983_of_match);

static struct i2c_driver hh983_driver = {
	.probe = hh983_probe,
	.remove = hh983_remove,
	.shutdown = hh983_shutdown,
	.id_table = hh983_id,
	.driver = {
		.name = "hh983-serializer",
		.of_match_table = hh983_of_match,
	},
};

module_i2c_driver(hh983_driver);

MODULE_DESCRIPTION("HH983 FPDLink Serializer Driver (983+984 / 983+988 / 983+988 video only)");
MODULE_AUTHOR("Albert David");
MODULE_LICENSE("GPL");
