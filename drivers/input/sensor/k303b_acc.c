/******************** (C) COPYRIGHT 2013 STMicroelectronics ********************
 *
 * File Name          : k303b_acc.c
 * Authors            : AMS - Motion Mems Division - Application Team
 *		      : Matteo Dameno (matteo.dameno@st.com)
 *		      : Denis Ciocca (denis.ciocca@st.com)
 *		      : Both authors are willing to be considered the contact
 *		      : and update points for the driver.
 * Version            : V.1.0.6_ST
 * Date               : 2014/Jun/18
 * Description        : K303B accelerometer driver
 *
 *******************************************************************************
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THE PRESENT SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES
 * OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, FOR THE SOLE
 * PURPOSE TO SUPPORT YOUR APPLICATION DEVELOPMENT.
 * AS A RESULT, STMICROELECTRONICS SHALL NOT BE HELD LIABLE FOR ANY DIRECT,
 * INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING FROM THE
 * CONTENT OF SUCH SOFTWARE AND/OR THE USE MADE BY CUSTOMERS OF THE CODING
 * INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
 *
 ******************************************************************************/

#include	<linux/err.h>
#include	<linux/errno.h>
#include	<linux/delay.h>
#include	<linux/fs.h>
#include	<linux/i2c.h>
#include	<linux/input.h>
#include	<linux/uaccess.h>
#include	<linux/workqueue.h>
#include	<linux/irq.h>
#include	<linux/gpio.h>
#include	<linux/interrupt.h>
#include	<linux/slab.h>
#include	<linux/kernel.h>
#include	<linux/device.h>
#include	<linux/module.h>
#include	<linux/moduleparam.h>
#include	<linux/regulator/consumer.h>
#include	<linux/of_gpio.h>

#include	"k303b.h"

#define LGE_ACCELEROMETER_NAME		"lge_accelerometer"
#define K303B_ACCEL_CALIBRATION  1
#ifdef K303B_ACCEL_CALIBRATION
#include <linux/syscalls.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/time.h>
#endif

#define G_MAX			(7995148) /* (SENSITIVITY_8G * ((2^15)-1)) */
#define G_MIN			(-7995392) /* (-SENSITIVITY_8G * (2^15)) */
#define FUZZ			0
#define FLAT			0
#define I2C_RETRY_DELAY		5
#define I2C_RETRIES		5
#define I2C_AUTO_INCREMENT	(0x00)

/* calibration result */
#define FAIL 0
#define SUCCESS 1
#define FLAT_FAIL 2
#define MS_TO_NS(x)		(x * 1000000L)

#define SENSITIVITY_2G		 61	/**	ug/LSB	*/
#define SENSITIVITY_4G		122	/**	ug/LSB	*/
#define SENSITIVITY_8G		244	/**	ug/LSB	*/

#define K303B_AXIS_X             0
#define K303B_AXIS_Y             1
#define K303B_AXIS_Z             2

/* Accelerometer Sensor Operating Mode */
#define K303B_ACC_ENABLE	(0x01)
#define K303B_ACC_DISABLE	(0x00)

#define AXISDATA_REG		(0x28)
#define WHOAMI_K303B_ACC	(0x41)	/*	Expctd content for WAI	*/
#define ALL_ZEROES		(0x00)
#define K303B_ACC_PM_OFF	(0x00)
#define K303B_ACC_PM_ON	 (0x3F)
#define ACC_ENABLE_ALL_AXES	(0x07)

/*	CONTROL REGISTERS	*/
#define TEMP_L			(0x0B)
#define TEMP_H			(0x0C)
#define WHO_AM_I		(0x0F)	/*	WhoAmI register		*/
#define ACT_THS			(0x1E)	/*	Activity Threshold	*/
#define ACT_DUR			(0x1F)	/*	Activity Duration	*/
/* ctrl 1: HR ODR2 ODR1 ODR0 BDU Zenable Yenable Xenable */
#define CTRL1			(0x20)	/*	control reg 1		*/
#define CTRL2			(0x21)	/*	control reg 2		*/
#define CTRL3			(0x22)	/*	control reg 3		*/
#define CTRL4			(0x23)	/*	control reg 4		*/
#define CTRL5			(0x24)	/*	control reg 5		*/
#define CTRL6			(0x25)	/*	control reg 6		*/
#define CTRL7			(0x26)	/*	control reg 7		*/

#define FIFO_CTRL		(0x2E)	/*	fifo control reg	*/

#define INT_CFG1		(0x30)	/*	interrupt 1 config	*/
#define INT_SRC1		(0x31)	/*	interrupt 1 source	*/
#define INT_THSX1		(0x32)	/*	interrupt 1 threshold x	*/
#define INT_THSY1		(0x33)	/*	interrupt 1 threshold y	*/
#define INT_THSZ1		(0x34)	/*	interrupt 1 threshold z	*/
#define INT_DUR1		(0x35)	/*	interrupt 1 duration	*/

#define INT_CFG2		(0x36)	/*	interrupt 2 config	*/
#define INT_SRC2		(0x37)	/*	interrupt 2 source	*/
#define INT_THS2		(0x38)	/*	interrupt 2 threshold	*/
#define INT_DUR2		(0x39)	/*	interrupt 2 duration	*/

#define REF_XL			(0x3A)	/*	reference_l_x		*/
#define REF_XH			(0x3B)	/*	reference_h_x		*/
#define REF_YL			(0x3C)	/*	reference_l_y		*/
#define REF_YH			(0x3D)	/*	reference_h_y		*/
#define REF_ZL			(0x3E)	/*	reference_l_z		*/
#define REF_ZH			(0x3F)	/*	reference_h_z		*/
/*	end CONTROL REGISTRES	*/



#define ACC_ODR10		(0x10)	/*   10Hz output data rate */
#define ACC_ODR50		(0x20)	/*   50Hz output data rate */
#define ACC_ODR100		(0x30)	/*  100Hz output data rate */
#define ACC_ODR200		(0x40)	/*  200Hz output data rate */
#define ACC_ODR400		(0x50)	/*  400Hz output data rate */
#define ACC_ODR800		(0x60)	/*  800Hz output data rate */
#define ACC_ODR_MASK		(0X70)

/* Registers configuration Mask and settings */
/* CTRL1 */
#define CTRL1_HR_DISABLE	(0x00)
#define CTRL1_HR_ENABLE		(0x80)
#define CTRL1_HR_MASK		(0x80)
#define CTRL1_BDU_ENABLE	(0x08)
#define CTRL1_BDU_MASK		(0x08)

/* CTRL2 */
#define CTRL2_IG1_INT1		(0x08)

/* CTRL3 */
#define CTRL3_IG1_INT1		(0x08)
#define CTRL3_DRDY_INT1

/* CTRL4 */
#define CTRL4_IF_ADD_INC_EN	(0x04)
#define CTRL4_FULL_SCALE_4G	(0x20)
#define CTRL4_BW_SCALE_ODR_AUT	(0x00)
#define CTRL4_BW_SCALE_ODR_SEL	(0x08)
#define CTRL4_ANTALIAS_BW_400	(0x00)
#define CTRL4_ANTALIAS_BW_200	(0x40)
#define CTRL4_ANTALIAS_BW_100	(0x80)
#define CTRL4_ANTALIAS_BW_50	(0xC0)
#define CTRL4_ANTALIAS_BW_MASK	(0xC0)

/* CTRL5 */
#define CTRL5_HLACTIVE_L	(0x02)
#define CTRL5_HLACTIVE_H	(0x00)

/* CTRL6 */
#define CTRL6_IG2_INT2		(0x10)
#define CTRL6_DRDY_INT2		(0x01)

/* CTRL7 */
#define CTRL7_LIR2		(0x08)
#define CTRL7_LIR1		(0x04)
/* */

#define NO_MASK			(0xFF)

#define INT1_DURATION_MASK	(0x7F)
#define INT1_THRESHOLD_MASK	(0x7F)



/* RESUME STATE INDICES */
#define RES_CTRL1		0
#define RES_CTRL2		1
#define RES_CTRL3		2
#define RES_CTRL4		3
#define RES_CTRL5		4
#define RES_CTRL6		5
#define RES_CTRL7		6

#define RES_INT_CFG1		7
#define RES_INT_THSX1		8
#define RES_INT_THSY1		9
#define RES_INT_THSZ1		10
#define RES_INT_DUR1		11


#define RES_INT_CFG2		12
#define RES_INT_THS2		13
#define RES_INT_DUR2		14

#define RES_TEMP_CFG_REG	15
#define RES_REFERENCE_REG	16
#define RES_FIFO_CTRL	17

#define RESUME_ENTRIES		18
/* end RESUME STATE INDICES */

#define OUTPUT_ALWAYS_ANTI_ALIASED 1

/*
	Self-Test threhold for 16bit/4G output
*/
#define	K303B_SHAKING_DETECT_THRESHOLD 1597/* 200->0.195g X 8192 LSB/g */

#define	TESTLIMIT_XY			(1475)/* 8192 LSB/g X 0.180g */
#define	TESTLIMIT_Z_USL_LSB		(10240)/* 8192 LSB + 8192 LSB/g X 0.250g */
#define	TESTLIMIT_Z_LSL_LSB		(6144)/* 8192 LSB - 8192 LSB/g X 0.250g */

#define HWST_LSL_LSB			(573)/* 8192LSB * 0.07g */
#define HWST_USL_LSB			(12288)/* 8192LSB * 1.5g */

#define	CALIBRATION_DATA_AMOUNT	10

struct {
	unsigned int cutoff_ms;
	unsigned int mask;
} k303b_acc_odr_table[] = {
		{    2, ACC_ODR800 },
		{    3, ACC_ODR400  },
		{    5, ACC_ODR200  },
		{   10, ACC_ODR100  },
#if (!OUTPUT_ALWAYS_ANTI_ALIASED)
		{   20, ACC_ODR50   },
		{  100, ACC_ODR10   },
#endif
};

static int int1_gpio = K303B_ACC_DEFAULT_INT1_GPIO;
module_param(int1_gpio, int, S_IRUGO);
struct k303b_acc {
	s16 x;
	s16 y;
	s16 z;
	int bCalLoaded;
};

struct k303b_acc_status {
	struct i2c_client *client;
	struct k303b_acc_platform_data *pdata;

	struct mutex lock;
	struct work_struct input_poll_work;
	struct hrtimer hr_timer_poll;
	ktime_t polling_ktime;
	struct workqueue_struct *hr_timer_poll_work_queue;

	struct input_dev *input_dev;
#ifdef K303B_ACCEL_CALIBRATION
	struct k303b_acc cal_data;
#endif
	int hw_initialized;
	atomic_t selftest_rslt;
	/* hw_working=-1 means not tested yet */
	int hw_working;
	atomic_t enabled;
	int on_before_suspend;
	int use_smbus;

	u8 sensitivity;

	u8 resume_state[RESUME_ENTRIES];

	int irq1;
	struct work_struct irq1_work;
	struct workqueue_struct *irq1_work_queue;
	int irq2;
	struct work_struct irq2_work;
	struct workqueue_struct *irq2_work_queue;

#ifdef K303B_ACCEL_CALIBRATION
	atomic_t fast_calib_x_rslt;
	atomic_t fast_calib_y_rslt;
	atomic_t fast_calib_z_rslt;
	atomic_t fast_calib_rslt;
#endif

#ifdef DEBUG
	u8 reg_addr;
#endif

};

static struct k303b_acc_platform_data default_k303b_acc_pdata = {
	.fs_range = K303B_ACC_FS_4G,
	.axis_map_x = 1,
	.axis_map_y = 0,
	.axis_map_z = 2,
	.negate_x = 0,
	.negate_y = 1,
	.negate_z = 1,
	.poll_interval = 100,
	.min_interval = K303B_ACC_MIN_POLL_PERIOD_MS,
	.gpio_int1 = K303B_ACC_DEFAULT_INT1_GPIO,
};

/* sets default init values to be written in registers at probe stage */
static void k303b_acc_set_init_register_values(
						struct k303b_acc_status *stat)
{
	memset(stat->resume_state, 0, ARRAY_SIZE(stat->resume_state));

	stat->resume_state[RES_CTRL1] = (ALL_ZEROES | CTRL1_HR_DISABLE | CTRL1_BDU_ENABLE | ACC_ENABLE_ALL_AXES);

	if (stat->pdata->gpio_int1 >= 0)
		stat->resume_state[RES_CTRL3] = (stat->resume_state[RES_CTRL3] | CTRL3_IG1_INT1);

	stat->resume_state[RES_CTRL4] = (ALL_ZEROES | CTRL4_IF_ADD_INC_EN | CTRL4_FULL_SCALE_4G);

	stat->resume_state[RES_CTRL5] = (ALL_ZEROES | CTRL5_HLACTIVE_H);

	stat->resume_state[RES_CTRL7] = (ALL_ZEROES | CTRL7_LIR2 | CTRL7_LIR1);

}

static int k303b_acc_i2c_read(struct k303b_acc_status *stat, u8 *buf, int len)
{
	int ret;
	u8 reg = buf[0];
	u8 cmd = reg;

	if (len > 1)
		cmd = (I2C_AUTO_INCREMENT | reg);
	if (stat->use_smbus) {
		if (len == 1) {
			ret = i2c_smbus_read_byte_data(stat->client, cmd);
			buf[0] = ret & 0xff;
#ifdef DEBUG
			dev_warn(&stat->client->dev,
				"i2c_smbus_read_byte_data: ret=0x%02x, len:%d , command=0x%02x, buf[0]=0x%02x\n",
				ret, len, cmd , buf[0]);
#endif
		} else if (len > 1) {
			ret = i2c_smbus_read_i2c_block_data(stat->client, cmd, len, buf);
		} else
			ret = -1;

		if (ret < 0) {
			dev_err(&stat->client->dev, "read transfer error: len:%d, command=0x%02x\n", len, cmd);
			return 0; /* failure */
		}
		return len; /* success */
	}

	ret = i2c_master_send(stat->client, &cmd, sizeof(cmd));
	if (ret != sizeof(cmd))
		return ret;

	return i2c_master_recv(stat->client, buf, len);
}

static int k303b_acc_i2c_write(struct k303b_acc_status *stat, u8 *buf, int len)
{
	int ret;
	u8 reg, value;

	if (len > 1)
		buf[0] = (I2C_AUTO_INCREMENT | buf[0]);

	reg = buf[0];
	value = buf[1];

	if (stat->use_smbus) {
		if (len == 1) {
			ret = i2c_smbus_write_byte_data(stat->client, reg, value);
#ifdef DEBUG
			dev_warn(&stat->client->dev,
				"i2c_smbus_write_byte_data: ret=%d, len:%d, command=0x%02x, value=0x%02x\n",
				ret, len, reg , value);
#endif
			return ret;
		} else if (len > 1) {
			ret = i2c_smbus_write_i2c_block_data(stat->client, reg, len, buf + 1);
			return ret;
		}
	}

	ret = i2c_master_send(stat->client, buf, len+1);
	return (ret == len+1) ? 0 : ret;
}

static int k303b_acc_hw_init(struct k303b_acc_status *stat)
{
	int err = -1;
    u8 buf[7], retry = 3;

	pr_info("%s: hw init start\n", K303B_ACC_DEV_NAME);


err_retry_whoami:
	buf[0] = WHO_AM_I;
	err = k303b_acc_i2c_read(stat, buf, 1);
	if (err < 0) {
		dev_warn(&stat->client->dev, "Error reading WHO_AM_I: is device available/working?\n");
		goto err_firstread;
	} else
		stat->hw_working = 1;

	if (buf[0] != WHOAMI_K303B_ACC) {
		dev_err(&stat->client->dev,
			"device unknown. Expected: 0x%02x, Replies: 0x%02x\n", WHOAMI_K303B_ACC, buf[0]);
		err = -1; /* choose the right coded error */
        if (--retry <= 0) {
            goto err_unknown_device;
        } else {
            mdelay(5);
            goto err_retry_whoami;
        }
	}

	buf[0] = CTRL4;
	buf[1] = stat->resume_state[RES_CTRL4];
	err = k303b_acc_i2c_write(stat, buf, 1);
	if (err < 0)
		goto err_resume_state;

	buf[0] = FIFO_CTRL;
	buf[1] = stat->resume_state[RES_FIFO_CTRL];
	err = k303b_acc_i2c_write(stat, buf, 1);
	if (err < 0)
		goto err_resume_state;

	buf[0] = INT_THSX1;
	buf[1] = stat->resume_state[RES_INT_THSX1];
	buf[2] = stat->resume_state[RES_INT_THSY1];
	buf[3] = stat->resume_state[RES_INT_THSZ1];
	buf[4] = stat->resume_state[RES_INT_DUR1];
	err = k303b_acc_i2c_write(stat, buf, 4);
	if (err < 0)
		goto err_resume_state;
	buf[0] = INT_CFG1;
	buf[1] = stat->resume_state[RES_INT_CFG1];
	err = k303b_acc_i2c_write(stat, buf, 1);
	if (err < 0)
		goto err_resume_state;


	buf[0] = CTRL2;
	buf[1] = stat->resume_state[RES_CTRL2];
	buf[2] = stat->resume_state[RES_CTRL3];
	err = k303b_acc_i2c_write(stat, buf, 2);
	if (err < 0)
		goto err_resume_state;

	buf[0] = CTRL5;
	buf[1] = stat->resume_state[RES_CTRL5];
	buf[2] = stat->resume_state[RES_CTRL6];
	buf[3] = stat->resume_state[RES_CTRL7];
	err = k303b_acc_i2c_write(stat, buf, 3);
	if (err < 0)
		goto err_resume_state;

	buf[0] = CTRL1;
	buf[1] = stat->resume_state[RES_CTRL1];
	err = k303b_acc_i2c_write(stat, buf, 1);
	if (err < 0)
		goto err_resume_state;

	stat->hw_initialized = 1;
	pr_info("%s: hw init done\n", K303B_ACC_DEV_NAME);
	return 0;

err_firstread:
	stat->hw_working = 0;
err_unknown_device:
err_resume_state:
	stat->hw_initialized = 0;
	dev_err(&stat->client->dev, "hw init error 0x%02x,0x%02x: %d\n", buf[0], buf[1], err);
	return err;
}

static void k303b_acc_device_power_off(struct k303b_acc_status *stat)
{
	int err;
	u8 buf[2] = { CTRL1, K303B_ACC_PM_OFF };

	err = k303b_acc_i2c_write(stat, buf, 1);
	if (err < 0)
		dev_err(&stat->client->dev, "soft power off failed: %d\n", err);

	if (stat->pdata->power_off) {
		if (stat->pdata->gpio_int1 >= 0)
			disable_irq_nosync(stat->irq1);

		stat->pdata->power_off(stat->pdata);
	}
	if (stat->hw_initialized) {
		if (stat->pdata->gpio_int1 >= 0)
			disable_irq_nosync(stat->irq1);

	}

}

static int k303b_acc_device_power_on(struct k303b_acc_status *stat)
{
	int err = -1;
	u8 ctrl1_buf[2] = { CTRL1, K303B_ACC_PM_ON };
	u8 ctrl4_buf[2] = { CTRL4, ALL_ZEROES | CTRL4_IF_ADD_INC_EN | CTRL4_FULL_SCALE_4G };

	if (stat->pdata->power_on) {
		err = stat->pdata->power_on(stat->pdata);
		if (err < 0) {
			dev_err(&stat->client->dev, "power_on failed: %d\n", err);
			return err;
		}
		if (stat->pdata->gpio_int1 >= 0)
			enable_irq(stat->irq1);
	}

	if (!stat->hw_initialized) {
		err = k303b_acc_hw_init(stat);
		if (stat->hw_working == 1 && err < 0) {
			k303b_acc_device_power_off(stat);
			return err;
        }
    } else {
		err = k303b_acc_i2c_write(stat, ctrl1_buf, 1);
		if (err < 0) {
			dev_err(&stat->client->dev, "soft power on failed: %d\n", err);
			return err;
		}
        err = k303b_acc_i2c_write(stat, ctrl4_buf, 1);
		if (err < 0) {
			dev_err(&stat->client->dev, "full scale set failed: %d\n", err);
			return err;
		}
	}
	if (stat->hw_initialized) {
		/* To remove warning message */
		//if (stat->pdata->gpio_int1 >= 0)
		//	enable_irq(stat->irq1);
	}
	return 0;
}

static int k303b_acc_update_fs_range(struct k303b_acc_status *stat, u8 new_fs_range)
{
	int err = -1;

	u8 sensitivity;
	u8 buf[2];
	u8 updated_val;
	u8 init_val;
	u8 new_val;
	u8 mask = K303B_ACC_FS_MASK;

	switch (new_fs_range) {
	case K303B_ACC_FS_2G:
		sensitivity = SENSITIVITY_2G;
		break;
	case K303B_ACC_FS_4G:
		sensitivity = SENSITIVITY_4G;
		break;
	case K303B_ACC_FS_8G:
		sensitivity = SENSITIVITY_8G;
		break;
	default:
		dev_err(&stat->client->dev, "invalid fs range requested: %u\n", new_fs_range);
		return -EINVAL;
	}


	/* Updates configuration register 4,
	* which contains fs range setting */
	buf[0] = CTRL4;
	err = k303b_acc_i2c_read(stat, buf, 1);
	if (err < 0)
		goto error;
	init_val = buf[0];
	stat->resume_state[RES_CTRL4] = init_val;
	new_val = new_fs_range;
	updated_val = ((mask & new_val) | ((~mask) & init_val));
	buf[1] = updated_val;
	buf[0] = CTRL4;
	err = k303b_acc_i2c_write(stat, buf, 1);
	if (err < 0)
		goto error;
	stat->resume_state[RES_CTRL4] = updated_val;
	stat->sensitivity = sensitivity;

	return err;
error:
	dev_err(&stat->client->dev, "update fs range failed 0x%02x,0x%02x: %d\n", buf[0], buf[1], err);

	return err;
}

#if 0
static int k303b_acc_update_odr(struct k303b_acc_status *stat, int poll_interval_ms)
{
	int err;
	int i;
	u8 config[2];
	u8 updated_val;
	u8 init_val;
	u8 new_val;
	u8 mask = ACC_ODR_MASK;

	/* Following, looks for the longest possible odr interval scrolling the
	 * odr_table vector from the end (shortest interval) backward (longest
	 * interval), to support the poll_interval requested by the system.
	 * It must be the longest interval lower then the poll interval.*/
	for (i = ARRAY_SIZE(k303b_acc_odr_table) - 1; i >= 0; i--) {
		if ((k303b_acc_odr_table[i].cutoff_ms <= poll_interval_ms) || (i == 0))
			break;
	}
	new_val = k303b_acc_odr_table[i].mask;

	/* Updates configuration register 1,
	* which contains odr range setting if enabled,
	* otherwise updates RES_CTRL1 for when it will */
	if (atomic_read(&stat->enabled)) {
		config[0] = CTRL1;
		err = k303b_acc_i2c_read(stat, config, 1);
		if (err < 0)
			goto error;
		init_val = config[0];
		stat->resume_state[RES_CTRL1] = init_val;
		updated_val = ((mask & new_val) | ((~mask) & init_val));
		config[1] = updated_val;
		config[0] = CTRL1;
		err = k303b_acc_i2c_write(stat, config, 1);
		if (err < 0)
			goto error;
		stat->resume_state[RES_CTRL1] = updated_val;
		return err;
	} else {
		init_val = stat->resume_state[RES_CTRL1];
		updated_val = ((mask & new_val) | ((~mask) & init_val));
		stat->resume_state[RES_CTRL1] = updated_val;
		return 0;
	}

error:
	dev_err(&stat->client->dev, "update odr failed 0x%02x,0x%02x: %d\n", config[0], config[1], err);
	return err;
}
#endif


static int k303b_acc_register_write(struct k303b_acc_status *stat, u8 *buf, u8 reg_address, u8 new_value)
{
	int err = -1;

	/* Sets configuration register at reg_address
	 *  NOTE: this is a straight overwrite  */
	buf[0] = reg_address;
	buf[1] = new_value;
	err = k303b_acc_i2c_write(stat, buf, 1);
	if (err < 0)
		return err;
	return err;
}


static int k303b_acc_get_raw(struct k303b_acc_status *stat, int *xyz)
{
	int err = -1;
	/* Data bytes from hardware xL, xH, yL, yH, zL, zH */
	u8 acc_data[6];
	/* x,y,z hardware data */
	s32 hw_d[3] = { 0 };

	acc_data[0] = (AXISDATA_REG);

	mutex_lock(&stat->lock);
	err = k303b_acc_i2c_read(stat, acc_data, 6);
	if (err < 0) {
		mutex_unlock(&stat->lock);
		return err;
    }
	hw_d[0] = ((s16) ((acc_data[1] << 8) | acc_data[0]));
	hw_d[1] = ((s16) ((acc_data[3] << 8) | acc_data[2]));
	hw_d[2] = ((s16) ((acc_data[5] << 8) | acc_data[4]));
/*
	hw_d[0] = (hw_d[0] * acc->sensitivity) / 1000;
	hw_d[1] = (hw_d[1] * acc->sensitivity) / 1000;
	hw_d[2] = (hw_d[2] * acc->sensitivity) / 1000;
*/

	xyz[0] = ((stat->pdata->negate_x) ? (-hw_d[stat->pdata->axis_map_x])
		   : (hw_d[stat->pdata->axis_map_x]));
	xyz[1] = ((stat->pdata->negate_y) ? (-hw_d[stat->pdata->axis_map_y])
		   : (hw_d[stat->pdata->axis_map_y]));
	xyz[2] = ((stat->pdata->negate_z) ? (-hw_d[stat->pdata->axis_map_z])
		   : (hw_d[stat->pdata->axis_map_z]));
	mutex_unlock(&stat->lock);
	#ifdef DEBUG
	//	pr_info("%s read raw x=%d, y=%d, z=%d\n",
	//		K303B_ACC_DEV_NAME, xyz[0], xyz[1], xyz[2]);
	#endif
	return err;
}


static int k303b_acc_get_data(struct k303b_acc_status *stat, int *xyz)
{
	int err = -1;

    err = k303b_acc_get_raw(stat, xyz);
    if (err <0){
        pr_info("read_accel_xyz failed\n");
		return err;
    }

	xyz[0] -= stat->cal_data.x;
	xyz[1] -= stat->cal_data.y;
	xyz[2] -= stat->cal_data.z;

	return err;
}

static void k303b_acc_report_values(struct k303b_acc_status *stat, int *xyz)
{
	input_report_abs(stat->input_dev, ABS_X, xyz[0]);
	input_report_abs(stat->input_dev, ABS_Y, xyz[1]);
	input_report_abs(stat->input_dev, ABS_Z, xyz[2]);
	input_sync(stat->input_dev);
}

static void k303b_acc_report_triple(struct k303b_acc_status *stat)
{
	int err;
	int xyz[3];

	err = k303b_acc_get_data(stat, xyz);
	if (err < 0)
		dev_err(&stat->client->dev, "get_data failed\n");
	else
		k303b_acc_report_values(stat, xyz);
}
#ifdef K303B_ACCEL_CALIBRATION
static int k303b_read_calibration_data(struct i2c_client *client)
{
	int fd_offset_x, fd_offset_y, fd_offset_z;
	char offset_x_src[6],offset_y_src[6],offset_z_src[6];
	long offset_x,offset_y,offset_z;

	struct k303b_acc_status *stat = i2c_get_clientdata(client);

	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);

	memset(offset_x_src,0x00,sizeof(offset_x_src));
	memset(offset_y_src,0x00,sizeof(offset_y_src));
	memset(offset_z_src,0x00,sizeof(offset_z_src));

	fd_offset_x = sys_open("/sns/offset_x.dat",O_RDONLY, 0);
	fd_offset_y = sys_open("/sns/offset_y.dat",O_RDONLY, 0);
	fd_offset_z = sys_open("/sns/offset_z.dat",O_RDONLY, 0);

	if ((fd_offset_x < 0) || (fd_offset_y < 0) || (fd_offset_z < 0))
		return -EINVAL;

	if ((sys_read(fd_offset_x, offset_x_src, sizeof(offset_x_src)) < 0)
		||	(sys_read(fd_offset_y ,offset_y_src, sizeof(offset_y_src)) <0)
		||	(sys_read(fd_offset_z, offset_z_src, sizeof(offset_z_src)) <0 ))
		return -EINVAL;

	if ((strict_strtol(offset_x_src, 10, &offset_x))
		||	(strict_strtol(offset_y_src, 10, &offset_y))
		||	(strict_strtol(offset_z_src, 10, &offset_z)))
		return -EINVAL;

	if (offset_x > 8192 || offset_x < - 8192
		|| offset_y > 8192 || offset_y < -8192
		|| offset_z > 8192 || offset_z < -8192 ){
		dev_err(&client->dev,"Abnormal Calibration Data");
		offset_x = 0;
		offset_y = 0;
		offset_z = 0;
	}

	stat->cal_data.x = offset_x;
	stat->cal_data.y = offset_y;
	stat->cal_data.z = offset_z;

	sys_close(fd_offset_x);
	sys_close(fd_offset_y);
	sys_close(fd_offset_z);
	set_fs(old_fs);

	return 0;
}
#endif
static irqreturn_t k303b_acc_isr1(int irq, void *dev)
{
	struct k303b_acc_status *stat = dev;

	disable_irq_nosync(irq);
	queue_work(stat->irq1_work_queue, &stat->irq1_work);
	pr_debug("%s: isr1 queued\n", K303B_ACC_DEV_NAME);

	return IRQ_HANDLED;
}

static void k303b_acc_irq1_work_func(struct work_struct *work)
{
	struct k303b_acc_status *stat = container_of(work, struct k303b_acc_status, irq1_work);
	/* TODO  add interrupt service procedure.
		 ie:k303b_acc_get_int1_source(stat); */
	/* ; */
	pr_debug("%s: IRQ1 served\n", K303B_ACC_DEV_NAME);
/* exit: */
	enable_irq(stat->irq1);
}

static int k303b_acc_enable(struct k303b_acc_status *stat)
{
	int err;
	pr_info("k303b cal_data.bCalLoaded = %d\n",stat->cal_data.bCalLoaded);
    if( !(stat->cal_data.bCalLoaded ) ) {
		err = k303b_read_calibration_data(stat->client);
		if (err)
			pr_info("k303b calibration data not founded\n");
		else
			stat->cal_data.bCalLoaded = 1;
	}

	if (!atomic_cmpxchg(&stat->enabled, 0, 1)) {
		err = k303b_acc_device_power_on(stat);
		if (err < 0) {
			atomic_set(&stat->enabled, 0);
			return err;
		}
		stat->polling_ktime = ktime_set(stat->pdata->poll_interval / 1000,
				MS_TO_NS(stat->pdata->poll_interval % 1000));
		hrtimer_start(&stat->hr_timer_poll,
					stat->polling_ktime, HRTIMER_MODE_REL);
	}
#if 0
	// Setting scale range : +-/ 4g
	mutex_lock(&stat->lock);
	err = k303b_acc_update_fs_range(stat, K303B_ACC_FS_4G);
	if (err < 0) {
		mutex_unlock(&stat->lock);
		return err;
	}
	stat->pdata->fs_range = K303B_ACC_FS_4G;
	mutex_unlock(&stat->lock);
	dev_info(&stat->client->dev, "scale range set to: 0x%x (0x00 = 2G, 0x20 = 4G, 0x30 = 8G)\n", K303B_ACC_FS_4G);
#endif
	return 0;
}

static int k303b_acc_disable(struct k303b_acc_status *stat)
{
	if (atomic_cmpxchg(&stat->enabled, 1, 0)) {
		cancel_work_sync(&stat->input_poll_work);
		k303b_acc_device_power_off(stat);
	}

	return 0;
}


static ssize_t read_single_reg(struct device *dev, char *buf, u8 reg)
{
	struct k303b_acc_status *stat = dev_get_drvdata(dev);
	int err;

	u8 data = reg;
	err = k303b_acc_i2c_read(stat, &data, 1);
	if (err < 0)
		return err;

	return sprintf(buf, "0x%02x\n", data);
}

static int write_reg(struct device *dev, const char *buf, u8 reg, u8 mask, int resume_index)
{
	int err = -1;
	struct k303b_acc_status *stat = dev_get_drvdata(dev);
	u8 x[2];
	u8 new_val;
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	new_val = ((u8) val & mask);
	x[0] = reg;
	x[1] = new_val;
	err = k303b_acc_register_write(stat, x, reg, new_val);
	if (err < 0)
		return err;
	stat->resume_state[resume_index] = new_val;
	return err;
}

static ssize_t attr_get_polling_rate(struct device *dev, struct device_attribute *attr, char *buf)
{
	int val;

	struct k303b_acc_status *stat = dev_get_drvdata(dev);
	mutex_lock(&stat->lock);
	val = stat->pdata->poll_interval;
	mutex_unlock(&stat->lock);

	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_set_polling_rate(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	//int err;
	struct k303b_acc_status *stat = dev_get_drvdata(dev);
	unsigned long interval_ms;

	if (kstrtoul(buf, 10, &interval_ms))
		return -EINVAL;
	if (!interval_ms)
		return -EINVAL;
	interval_ms = max_t(unsigned int, (unsigned int)interval_ms, stat->pdata->min_interval);
	mutex_lock(&stat->lock);
	stat->pdata->poll_interval = interval_ms;
   
#if 0
	err = k303b_acc_update_odr(stat, interval_ms);
	if (err >= 0) {
		stat->pdata->poll_interval = interval_ms;
		stat->polling_ktime = ktime_set(stat->pdata->poll_interval / 1000,
				MS_TO_NS(stat->pdata->poll_interval % 1000));
	}
#endif

	stat->pdata->poll_interval = interval_ms;
	stat->polling_ktime = ktime_set(stat->pdata->poll_interval / 1000,
			MS_TO_NS(stat->pdata->poll_interval % 1000));

	mutex_unlock(&stat->lock);
	return size;
}

static ssize_t attr_get_range(struct device *dev, struct device_attribute *attr, char *buf)
{
	char val;
	struct k303b_acc_status *stat = dev_get_drvdata(dev);
	char range = 2;

	mutex_lock(&stat->lock);
	val = stat->pdata->fs_range;

	switch (val) {
	case K303B_ACC_FS_2G:
		range = 2;
		break;
	case K303B_ACC_FS_4G:
		range = 4;
		break;
	case K303B_ACC_FS_8G:
		range = 8;
		break;
	}

	mutex_unlock(&stat->lock);

	return sprintf(buf, "%d\n", range);
}

static ssize_t attr_set_range(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	struct k303b_acc_status *stat = dev_get_drvdata(dev);
	unsigned long val;
	u8 range;
	int err;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	switch (val) {
	case 2:
		range = K303B_ACC_FS_2G;
		break;
	case 4:
		range = K303B_ACC_FS_4G;
		break;
	case 8:
		range = K303B_ACC_FS_8G;
		break;
	default:
		dev_err(&stat->client->dev, "invalid range request: %lu, discarded\n", val);
		return -EINVAL;
	}

	mutex_lock(&stat->lock);
	err = k303b_acc_update_fs_range(stat, range);
	if (err < 0) {
		mutex_unlock(&stat->lock);
		return err;
	}
	stat->pdata->fs_range = range;
	mutex_unlock(&stat->lock);
	dev_info(&stat->client->dev, "range set to: %lu g\n", val);

	return size;
}

static ssize_t attr_get_enable(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct k303b_acc_status *stat = dev_get_drvdata(dev);
	int val = atomic_read(&stat->enabled);

	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_set_enable(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	struct k303b_acc_status *stat = dev_get_drvdata(dev);
	unsigned long val;
	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	if (val)
		k303b_acc_enable(stat);
	else
		k303b_acc_disable(stat);

	return size;
}

static ssize_t attr_get_raw_data(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct k303b_acc_status *stat = dev_get_drvdata(dev);
	int val = atomic_read(&stat->enabled);
	int data[3] = {0, 0, 0};

	if (val > 0) {
		k303b_acc_get_raw(stat, data);
	}

	return sprintf(buf, "data = %d, %d, %d\n", data[0], data[1], data[2]);
}
static ssize_t attr_get_sensor_cal_data(struct device *dev, struct device_attribute *attr, char *buf)
{
	int xyz[3] = { 0, };
	int err = 0;
	struct k303b_acc_status *stat = dev_get_drvdata(dev);
	err = k303b_acc_get_data(stat, xyz);
	if (err < 0)
		return sprintf(buf, "Read Calibrated Data failed");
	
    
	return sprintf(buf, "Read Calibrated Data x=%d y=%d z=%d \n",
			xyz[K303B_AXIS_X], xyz[K303B_AXIS_Y], xyz[K303B_AXIS_Z]);
}
static ssize_t attr_set_intconfig1(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	return write_reg(dev, buf, INT_CFG1, NO_MASK, RES_INT_CFG1);
}

static ssize_t attr_get_intconfig1(struct device *dev, struct device_attribute *attr, char *buf)
{
	return read_single_reg(dev, buf, INT_CFG1);
}

static ssize_t attr_set_duration1(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	return write_reg(dev, buf, INT_DUR1, INT1_DURATION_MASK, RES_INT_DUR1);
}

static ssize_t attr_get_duration1(struct device *dev, struct device_attribute *attr, char *buf)
{
	return read_single_reg(dev, buf, INT_DUR1);
}

static ssize_t attr_set_threshx1(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	return write_reg(dev, buf, INT_THSX1, INT1_THRESHOLD_MASK, RES_INT_THSX1);
}

static ssize_t attr_get_threshx1(struct device *dev, struct device_attribute *attr,	char *buf)
{
	return read_single_reg(dev, buf, INT_THSX1);
}

static ssize_t attr_set_threshy1(struct device *dev, struct device_attribute *attr,	const char *buf, size_t size)
{
	return write_reg(dev, buf, INT_THSY1, INT1_THRESHOLD_MASK, RES_INT_THSY1);
}

static ssize_t attr_get_threshy1(struct device *dev, struct device_attribute *attr,	char *buf)
{
	return read_single_reg(dev, buf, INT_THSY1);
}

static ssize_t attr_set_threshz1(struct device *dev, struct device_attribute *attr,	const char *buf, size_t size)
{
	return write_reg(dev, buf, INT_THSZ1, INT1_THRESHOLD_MASK, RES_INT_THSZ1);
}

static ssize_t attr_get_threshz1(struct device *dev, struct device_attribute *attr,	char *buf)
{
	return read_single_reg(dev, buf, INT_THSZ1);
}

static ssize_t attr_get_source1(struct device *dev, struct device_attribute *attr, char *buf)
{
	return read_single_reg(dev, buf, INT_SRC1);
}


#define XL_SELF_TEST_2G_MAX_LSB	(24576)
#define XL_SELF_TEST_2G_MIN_LSB	(1146)

static int k303b_acc_get_selftest(struct k303b_acc_status *stat, char *buf)
{
	int val, i, en_state = 0;

	u8 x[8];
	s32 NO_ST[3] = {0, 0, 0};
	s32 ST[3] = {0, 0, 0};

	en_state = atomic_read(&stat->enabled);
	k303b_acc_disable(stat);

	k303b_acc_device_power_on(stat);

	x[0] = CTRL1;
	x[1] = 0x3f;
	k303b_acc_i2c_write(stat, x, 1);
	x[0] = CTRL4;
	x[1] = 0x04;
	x[2] = 0x00;
	x[3] = 0x00;
	k303b_acc_i2c_write(stat, x, 3);

	mdelay(80);

	x[0] = AXISDATA_REG;
	k303b_acc_i2c_read(stat, x, 6);

	for (i = 0; i < 5; i++) {
		while (1) {
			x[0] = 0x27;
			val = k303b_acc_i2c_read(stat, x, 1);
			if (val < 0) {
				pr_info("I2C fail. (%d)\n", val);
				goto ST_EXIT;
			}
			if (x[0] & 0x08)
				break;
		}
		x[0] = AXISDATA_REG;
		k303b_acc_i2c_read(stat, x, 6);
		NO_ST[0] += (s16)((x[1] << 8) | x[0]);
		NO_ST[1] += (s16)((x[3] << 8) | x[2]);
		NO_ST[2] += (s16)((x[5] << 8) | x[4]);
	}
	NO_ST[0] /= 5;
	NO_ST[1] /= 5;
	NO_ST[2] /= 5;

	x[0] = CTRL5;
	x[1] = 0x04;
	k303b_acc_i2c_write(stat, x, 1);

	mdelay(80);

	x[0] = AXISDATA_REG;
	k303b_acc_i2c_read(stat, x, 6);

	for (i = 0; i < 5; i++) {
		while (1) {
			x[0] = 0x27;
			val = k303b_acc_i2c_read(stat, x, 1);
			if (val < 0) {
				pr_info("I2C fail. (%d)\n", val);
				goto ST_EXIT;
			}
			if (x[0] & 0x08)
				break;
		}
		x[0] = AXISDATA_REG;
		k303b_acc_i2c_read(stat, x, 6);
		ST[0] += (s16)((x[1] << 8) | x[0]);
		ST[1] += (s16)((x[3] << 8) | x[2]);
		ST[2] += (s16)((x[5] << 8) | x[4]);
	}
	ST[0] /= 5;
	ST[1] /= 5;
	ST[2] /= 5;

	for (val = 1, i = 0; i < 3; i++) {
		ST[i] -= NO_ST[i];
		ST[i] = abs(ST[i]);

		if ((XL_SELF_TEST_2G_MIN_LSB > ST[i]) || (ST[i] > XL_SELF_TEST_2G_MAX_LSB)) {
			pr_info("ST[%d]: Out of range!! (%d)\n", i, ST[i]);
			val = 0;
		}
	}

	if (val)
		pr_info("K303B Self test: OK (%d, %d, %d)\n", ST[0], ST[1], ST[2]);
	else
		pr_info("K303B Self test: NG (%d, %d, %d)\n", ST[0], ST[1], ST[2]);

ST_EXIT:
	x[0] = CTRL1;
	x[1] = 0x00;
	k303b_acc_i2c_write(stat, x, 1);
	x[0] = CTRL5;
	x[1] = 0x00;
	k303b_acc_i2c_write(stat, x, 1);

	k303b_acc_device_power_off(stat);

	if (en_state)
		k303b_acc_enable(stat);

	return val;
}
static ssize_t attr_get_selftest(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct k303b_acc_status *stat = dev_get_drvdata(dev);

	return sprintf(buf,"%s\n",atomic_read(&stat->selftest_rslt) ? "Pass" : "Fail");
}

static ssize_t attr_set_selftest(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned long data = 0;
	int error = -1;
	u8 buf_error[50];
	struct k303b_acc_status *stat = dev_get_drvdata(dev);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if(data == 1) {
		if(k303b_acc_get_selftest(stat,buf_error))
			atomic_set(&stat->selftest_rslt, 1);
		else
			atomic_set(&stat->selftest_rslt, 0);
	}
	else {
		pr_info("Selftest is failed\n");
		return -EINVAL;
	}

	return size;
}



#ifdef DEBUG
/* PAY ATTENTION: These DEBUG functions don't manage resume_state */
static ssize_t attr_reg_set(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int rc;
	struct k303b_acc_status *stat = dev_get_drvdata(dev);
	u8 x[2];
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	mutex_lock(&stat->lock);
	x[0] = stat->reg_addr;
	mutex_unlock(&stat->lock);
	x[1] = val;
	rc = k303b_acc_i2c_write(stat, x, 1);
	/*TODO: error need to be managed */
	return size;
}

static ssize_t attr_reg_get(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t ret;
	struct k303b_acc_status *stat = dev_get_drvdata(dev);
	int rc;
	u8 data;

	mutex_lock(&stat->lock);
	data = stat->reg_addr;
	mutex_unlock(&stat->lock);
	rc = k303b_acc_i2c_read(stat, &data, 1);
	/*TODO: error need to be managed */
	ret = sprintf(buf, "0x%02x\n", data);

	return ret;
}

static ssize_t attr_addr_set(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	struct k303b_acc_status *stat = dev_get_drvdata(dev);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	mutex_lock(&stat->lock);
	stat->reg_addr = val;
	mutex_unlock(&stat->lock);

	return size;
}
#endif

/* power on/off function*/
int k303b_power(struct k303b_acc_platform_data *pdata, int on)
{
	int ret =0;

	if (on) {
		//ret = regulator_enable(pdata->vdd_reg);
		//ret += regulator_enable(pdata->vdd_i2c);
		pr_info("k303b acc power on [status : %d ]\n", ret);
		//mdelay(30);
	} else {
		//ret = regulator_disable(pdata->vdd_reg);
		//ret += regulator_disable(pdata->vdd_i2c);
		pr_info("k303b acc power off [status : %d ]\n", ret);
	}
	return ret;
}

int k303b_power_on(struct k303b_acc_platform_data *pdata)
{
	int ret;
	ret =  k303b_power(pdata, 1);
	return ret;
}

int k303b_power_off(struct k303b_acc_platform_data *pdata)
{
	int ret;
	ret =  k303b_power(pdata, 0);
	return ret;
}
static int k303b_gpio_config(struct k303b_acc_status *stat)
{
	int ret;
	/* configure irq gpio */
	ret = gpio_request(stat->pdata->gpio_int1, "k303b_irq_gpio");
	if (ret) {
		dev_err(&stat->client->dev,
				"unable to request gpio [%d]\n",
				stat->pdata->gpio_int1);
	}
	ret = gpio_direction_input(stat->pdata->gpio_int1);
	if (ret) {
		dev_err(&stat->client->dev,
				"unable to set direction for gpio [%d]\n",
				stat->pdata->gpio_int1);
	}
	return ret;
}
/* LGE Add start DTS parse function */
#ifdef CONFIG_OF
static int k303b_parse_dt(struct device *dev, struct k303b_acc_platform_data *pdata)
{
	int rc;
	int ret, err = 0;
	struct device_node *np = dev->of_node;
	struct sensor_dt_to_pdata_map *itr;
	struct sensor_dt_to_pdata_map map[] = {
		//{"st,irq-gpio",     &pdata->gpio_int1,      DT_REQUIRED,        DT_GPIO,    0},
		{"st,fs_range",        &pdata->fs_range,        DT_SUGGESTED,       DT_U32,     0},
		{"st,axis_map_x",      &pdata->axis_map_x,     DT_SUGGESTED,       DT_U32,     0},
		{"st,axis_map_y",      &pdata->axis_map_y,     DT_SUGGESTED,       DT_U32,     0},
		{"st,axis_map_z",      &pdata->axis_map_z,     DT_SUGGESTED,       DT_U32,     0},
		{"st,negate_x",        &pdata->negate_x,       DT_SUGGESTED,       DT_U32,     0},
		{"st,negate_y",        &pdata->negate_y,       DT_SUGGESTED,       DT_U32,     0},
		{"st,negate_z",        &pdata->negate_z,       DT_SUGGESTED,       DT_U32,     0},
		{"st,poll_interval",   &pdata->poll_interval,  DT_SUGGESTED,       DT_U32,     0},
		{"st,min_interval",    &pdata->min_interval,   DT_SUGGESTED,       DT_U32,     0},
		{NULL,              NULL,                   0,                  0,          0},
	};

	for (itr = map; itr->dt_name ; ++itr) {
		switch (itr->type) {
			case DT_GPIO:
				ret = of_get_named_gpio(np, itr->dt_name, 0);
				if (ret >= 0) {
					*((int *) itr->ptr_data) = ret;
					ret = 0;
				}
				break;
			case DT_U32:
				ret = of_property_read_u32(np, itr->dt_name,
						(u32 *) itr->ptr_data);
				break;
			case DT_BOOL:
				*((bool *) itr->ptr_data) =
					of_property_read_bool(np, itr->dt_name);
				ret = 0;
				break;
			default:
				dev_err(dev,"%d is an unknown DT entry type\n",itr->type);
				ret = -EBADE;
		}
		dev_info(dev,"DT entry ret:%d name:%s val:%d\n",
				ret,itr->dt_name, *((int *)itr->ptr_data));
 
		if (ret) {
			*((int *)itr->ptr_data) = itr->default_val;
			if (itr->status < DT_OPTIONAL) {
				dev_err(dev,"Missing '%s' DT entry\n",itr->dt_name);
				/* cont on err to dump all missing entries */
				if (itr->status == DT_REQUIRED && !err)
					err = ret;
			}
		}
	}

    pdata->gpio_int1 = K303B_ACC_DEFAULT_INT1_GPIO;  //don't register gpio_int1

    pdata->vdd_reg = regulator_get(dev, "st,vdd_ana");
	if (IS_ERR(pdata->vdd_reg)) {
		rc = PTR_ERR(pdata->vdd_reg);
		dev_err(dev, "Regulator get failed vdd_ana-supply rc=%d\n", rc);
		return rc;
	}
	pdata->vdd_i2c = regulator_get(dev, "st,vddio_i2c");
	if (IS_ERR(pdata->vdd_i2c)) {
		rc = PTR_ERR(pdata->vdd_i2c);
		dev_err(dev, "Regulator get failed vddio_i2c-supply rc=%d\n", rc);
		return rc;
	}

    rc = regulator_enable(pdata->vdd_reg);
    mdelay(30);
    rc += regulator_enable(pdata->vdd_i2c);
    mdelay(80);
	/* debug print disable */
	/*
	   dev_info(dev, "parse_dt data [gpio_int = %d]\n", pdata->gpio_int);
	 */
return 0;
}
#else
static int k303b_parse_dt(struct device *dev, struct k303b_acc_platform_data *pdata)
{
	    return -ENODEV;
}
#endif
/*LGE Add end */

static int k303b_store_calibration_data(struct device *dev)
{
	unsigned char offset_x_src[6],offset_y_src[6],offset_z_src[6];
	int fd_offset_x, fd_offset_y, fd_offset_z;
	struct k303b_acc_status *stat = dev_get_drvdata(dev);
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);

	memset(offset_x_src,0x00,sizeof(offset_x_src));
	memset(offset_y_src,0x00,sizeof(offset_y_src));
	memset(offset_z_src,0x00,sizeof(offset_z_src));

	fd_offset_x = sys_open("/sns/offset_x.dat",O_WRONLY|O_CREAT, 0666);
	fd_offset_y = sys_open("/sns/offset_y.dat",O_WRONLY|O_CREAT, 0666);
	fd_offset_z = sys_open("/sns/offset_z.dat",O_WRONLY|O_CREAT, 0666);

#ifdef DEBUG
	dev_info(dev, "[fd_offset_x] writing %d", fd_offset_x);
	dev_info(dev, "[fd_offset_y] writing %d", fd_offset_y );
	dev_info(dev, "[fd_offset_z] writing %d", fd_offset_z);
#endif

	if ((fd_offset_x < 0) || (fd_offset_y < 0) || (fd_offset_z < 0))
		return -EINVAL;

	sprintf(offset_x_src, "%d", stat->cal_data.x);
	sprintf(offset_y_src, "%d", stat->cal_data.y);
	sprintf(offset_z_src, "%d", stat->cal_data.z);

#ifdef DEBUG
	dev_info(dev, "[offset_x] writing %s", offset_x_src);
	dev_info(dev, "[offset_y] writing %s", offset_y_src);
	dev_info(dev, "[offset_z] writing %s", offset_z_src);
#endif

	if ((sys_write(fd_offset_x, offset_x_src, sizeof(offset_x_src)) < 0)
			||	(sys_write(fd_offset_y, offset_y_src,  sizeof(offset_y_src)) < 0)
				||	(sys_write(fd_offset_z, offset_z_src,  sizeof(offset_z_src)) < 0))
		return -EINVAL;

	atomic_set(&stat->fast_calib_rslt, SUCCESS);

	sys_fsync(fd_offset_x);
	sys_fsync(fd_offset_y);
	sys_fsync(fd_offset_z);

	sys_close(fd_offset_x);
	sys_close(fd_offset_y);
	sys_close(fd_offset_z);
        sys_chmod("/sns/offset_x.dat", 0664);
        sys_chmod("/sns/offset_y.dat", 0664);
        sys_chmod("/sns/offset_z.dat", 0664);
	set_fs(old_fs);

#ifdef DEBUG
	dev_info(dev, "[CLOSE FILE]!!!!! ");
#endif

	return 0;
}

static int k303b_do_calibration(struct device *dev, char *axis)
{
	struct k303b_acc_status *stat = dev_get_drvdata(dev);
	unsigned int timeout_shaking = 0;
	int acc_cal_pre[3] = { 0, };
	int acc_cal[3] = { 0, };
	int sum[3] = { 0, };

	k303b_acc_get_raw(stat, acc_cal_pre);
	do{
		mdelay(20);
		k303b_acc_get_raw(stat, acc_cal);
		pr_info("=============== moved %s =============== timeout = %d\n", axis, timeout_shaking);
		pr_info("(%d, %d, %d) (%d, %d, %d)", acc_cal_pre[K303B_AXIS_X], acc_cal_pre[K303B_AXIS_Y], acc_cal_pre[K303B_AXIS_Z], acc_cal[K303B_AXIS_X], acc_cal[K303B_AXIS_Y], acc_cal[K303B_AXIS_Z]);

		if((abs(acc_cal[K303B_AXIS_X] - acc_cal_pre[K303B_AXIS_X]) > K303B_SHAKING_DETECT_THRESHOLD)
			|| (abs((acc_cal[K303B_AXIS_Y] - acc_cal_pre[K303B_AXIS_Y])) > K303B_SHAKING_DETECT_THRESHOLD)
			|| (abs((acc_cal[K303B_AXIS_Z] - acc_cal_pre[K303B_AXIS_Z])) > K303B_SHAKING_DETECT_THRESHOLD)){
				atomic_set(&stat->fast_calib_rslt, FAIL);
				pr_info("================ shaking %s ================\n", axis);
				return -EINVAL;
		}
		else {
			sum[K303B_AXIS_X] += acc_cal[K303B_AXIS_X];
			sum[K303B_AXIS_Y] += acc_cal[K303B_AXIS_Y];
			sum[K303B_AXIS_Z] += acc_cal[K303B_AXIS_Z];

			acc_cal_pre[K303B_AXIS_X] = acc_cal[K303B_AXIS_X];
			acc_cal_pre[K303B_AXIS_Y] = acc_cal[K303B_AXIS_Y];
			acc_cal_pre[K303B_AXIS_Z] = acc_cal[K303B_AXIS_Z];
		}
			timeout_shaking++;
	}while(timeout_shaking < CALIBRATION_DATA_AMOUNT);
	pr_info("==========complete shaking %s check==========\n", axis);

	/* check zero-g offset */
	if ((abs(sum[K303B_AXIS_X]/CALIBRATION_DATA_AMOUNT) >TESTLIMIT_XY) ||
		(abs(sum[K303B_AXIS_Y]/CALIBRATION_DATA_AMOUNT) >TESTLIMIT_XY) ||
		 ((sum[K303B_AXIS_Z]/CALIBRATION_DATA_AMOUNT > TESTLIMIT_Z_USL_LSB) || (sum[K303B_AXIS_Z]/CALIBRATION_DATA_AMOUNT < TESTLIMIT_Z_LSL_LSB))) {
			pr_err("Calibration zero-g offset check failed (%d, %d, %d)\n",
					sum[K303B_AXIS_X]/CALIBRATION_DATA_AMOUNT, sum[K303B_AXIS_Y]/CALIBRATION_DATA_AMOUNT, sum[K303B_AXIS_Z]/CALIBRATION_DATA_AMOUNT);
			atomic_set(&stat->fast_calib_rslt, FLAT_FAIL);

			return -EINVAL;
	}

	stat->cal_data.x = sum[0] / CALIBRATION_DATA_AMOUNT;
	stat->cal_data.y = sum[1] / CALIBRATION_DATA_AMOUNT;

	if (sum[2] >= 0) {
		stat->cal_data.z = (sum[2] / CALIBRATION_DATA_AMOUNT) - 8192; /* k303b(16bit) 8192@ 4g range */
	} else {
		stat->cal_data.z = (sum[2] / CALIBRATION_DATA_AMOUNT) + 8192; /* k303b(16bit) 8192@ 4g range */
	}

	pr_info("=============== %s fast calibration finished===============\n", axis);

	return 0;
}

static ssize_t attr_get_fast_calibration(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct k303b_acc_status *stat = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", atomic_read(&stat->fast_calib_rslt));
}

static ssize_t attr_set_fast_calibration(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	struct k303b_acc_status *stat = dev_get_drvdata(dev);
	unsigned int timeout_shaking = 0;
	unsigned long data;
	int acc_cal_pre[3] = { 0, };
	int acc_cal[3] = { 0, };
	int sum[3] = { 0, };
	int error = 0;

	atomic_set(&stat->fast_calib_rslt, FAIL);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	k303b_acc_get_raw(stat, acc_cal_pre);
	do{
		mdelay(20);
		k303b_acc_get_raw(stat, acc_cal);
		pr_info("===============moved x,y,z=============== timeout = %d\n",timeout_shaking);
		pr_info("(%d, %d, %d) (%d, %d, %d)", acc_cal_pre[K303B_AXIS_X], acc_cal_pre[K303B_AXIS_Y], acc_cal_pre[K303B_AXIS_Z], acc_cal[K303B_AXIS_X], acc_cal[K303B_AXIS_Y], acc_cal[K303B_AXIS_Z]);

		if((abs(acc_cal[K303B_AXIS_X] - acc_cal_pre[K303B_AXIS_X]) > K303B_SHAKING_DETECT_THRESHOLD)
			|| (abs((acc_cal[K303B_AXIS_Y] - acc_cal_pre[K303B_AXIS_Y])) > K303B_SHAKING_DETECT_THRESHOLD)
			|| (abs((acc_cal[K303B_AXIS_Z] - acc_cal_pre[K303B_AXIS_Z])) > K303B_SHAKING_DETECT_THRESHOLD)){
				atomic_set(&stat->fast_calib_rslt, FAIL);
				pr_info("===============shaking x,y,z===============\n");
				return -EINVAL;
		} else {
			sum[K303B_AXIS_X] += acc_cal[K303B_AXIS_X];
			sum[K303B_AXIS_Y] += acc_cal[K303B_AXIS_Y];
			sum[K303B_AXIS_Z] += acc_cal[K303B_AXIS_Z];

			acc_cal_pre[K303B_AXIS_X] = acc_cal[K303B_AXIS_X];
			acc_cal_pre[K303B_AXIS_Y] = acc_cal[K303B_AXIS_Y];
			acc_cal_pre[K303B_AXIS_Z] = acc_cal[K303B_AXIS_Z];
		}
			timeout_shaking++;
	}while(timeout_shaking < CALIBRATION_DATA_AMOUNT);
	pr_info("===============complete shaking x,y,z check===============\n");

	/* check zero-g offset */
	if ((abs(sum[K303B_AXIS_X]/CALIBRATION_DATA_AMOUNT) > TESTLIMIT_XY) ||
		(abs(sum[K303B_AXIS_Y]/CALIBRATION_DATA_AMOUNT) > TESTLIMIT_XY) ||
		 ((sum[K303B_AXIS_Z]/CALIBRATION_DATA_AMOUNT > TESTLIMIT_Z_USL_LSB) || (sum[K303B_AXIS_Z]/CALIBRATION_DATA_AMOUNT < TESTLIMIT_Z_LSL_LSB))) {
			pr_err("Calibration zero-g offset check failed (%d, %d, %d)\n",
					sum[K303B_AXIS_X]/CALIBRATION_DATA_AMOUNT, sum[K303B_AXIS_Y]/CALIBRATION_DATA_AMOUNT, sum[K303B_AXIS_Z]/CALIBRATION_DATA_AMOUNT);
			atomic_set(&stat->fast_calib_rslt, FLAT_FAIL);

			return -EINVAL;
	}

	stat->cal_data.x = sum[0] / CALIBRATION_DATA_AMOUNT;
	stat->cal_data.y = sum[1] / CALIBRATION_DATA_AMOUNT;

	if (sum[2] >= 0) {
		stat->cal_data.z = (sum[2] / CALIBRATION_DATA_AMOUNT) - 8192; /* k303b(16bit) 8192@ 4g range */
	} else {
		stat->cal_data.z = (sum[2] / CALIBRATION_DATA_AMOUNT) + 8192; /* k303b(16bit) 8192@ 4g range */
	}

	pr_info("===============x,y,z fast calibration finished===============\n");


	error = k303b_store_calibration_data(dev);
	if (error) {
		pr_err("k303b_fast_calibration_store failed");
		return error;
	}
	atomic_set(&stat->fast_calib_rslt, SUCCESS);

	return size;

}

static ssize_t attr_get_eeprom_writing(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct k303b_acc_status *stat = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", atomic_read(&stat->fast_calib_x_rslt));
}

static ssize_t attr_set_eeprom_writing(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int error = 0;
	error = k303b_store_calibration_data(dev);
	if (error)
		return error;

	return count;
}

static ssize_t attr_get_fast_calibration_x(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct k303b_acc_status *stat = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", atomic_read(&stat->fast_calib_x_rslt));
}

static ssize_t attr_set_fast_calibration_x(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)
{
	int error = 0;
	char axis = 'x';
	struct k303b_acc_status *stat = dev_get_drvdata(dev);

	atomic_set(&stat->fast_calib_rslt, FAIL);

	error = k303b_do_calibration(dev,&axis);
	if (error) {
		dev_err(dev,"k303b_do_calibration failed");
		return error;
	}
	error = k303b_store_calibration_data(dev);
	if (error) {
		dev_err(dev,"k303b_store_calibration_data failed");
		return error;
	}
	atomic_set(&stat->fast_calib_x_rslt, SUCCESS);
	return size;
}

static ssize_t attr_get_fast_calibration_y(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct k303b_acc_status *stat = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", atomic_read(&stat->fast_calib_y_rslt));
}

static ssize_t attr_set_fast_calibration_y(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)
{
	int error = 0;
	char axis = 'y';
	struct k303b_acc_status *stat = dev_get_drvdata(dev);

	atomic_set(&stat->fast_calib_y_rslt, FAIL);

	error = k303b_do_calibration(dev,&axis);
	if (error) {
		dev_err(dev,"k303b_do_calibration failed");
		return error;
	}
	error = k303b_store_calibration_data(dev);
	if (error) {
		dev_err(dev,"k303b_store_calibration_data failed");
		return error;
	}
	atomic_set(&stat->fast_calib_y_rslt, SUCCESS);
	return size;
}

static ssize_t attr_get_fast_calibration_z(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct k303b_acc_status *stat = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", atomic_read(&stat->fast_calib_z_rslt));
}

static ssize_t attr_set_fast_calibration_z(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)
{
	int error = 0;
	char axis = 'z';
	struct k303b_acc_status *stat = dev_get_drvdata(dev);

	atomic_set(&stat->fast_calib_z_rslt, FAIL);

	error = k303b_do_calibration(dev,&axis);
	if (error) {
		dev_err(dev,"k303b_do_calibration failed");
		return error;
	}
	error = k303b_store_calibration_data(dev);
	if (error) {
		dev_err(dev,"k303b_store_calibration_data failed");
		return error;
	}
	atomic_set(&stat->fast_calib_z_rslt, SUCCESS);
	return size;
}
#if 1 /* LGE_SENSOR_FACTORY_AAT */
static ssize_t attr_get_cal_offset(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t ret;
	struct k303b_acc_status *stat = dev_get_drvdata(dev);
	int data;

	pr_info("ST 1\n");

	mutex_lock(&stat->lock);
	pr_info("ST 2\n");
	data = atomic_read(&stat->fast_calib_rslt);
	pr_info("ST 3\n");
	mutex_unlock(&stat->lock);
	pr_info("ST 4\n");

	ret = sprintf(buf, "%d %d %d\n", stat->cal_data.x, stat->cal_data.y, stat->cal_data.z);
	return ret;

}

/*
static ssize_t attr_set_dummy(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	return size;
}

static ssize_t attr_get_dummy(struct device *dev, struct device_attribute *attr, char *buf)
{
	return 0;
}
*/

static DEVICE_ATTR(enable, 0666, attr_get_enable, attr_set_enable);
static DEVICE_ATTR(poll_delay, 0666, attr_get_polling_rate, attr_set_polling_rate);
static DEVICE_ATTR(range, 0666, attr_get_range, attr_set_range);
static DEVICE_ATTR(value, 0666, attr_get_sensor_cal_data, NULL);
static DEVICE_ATTR(raw, 0666, attr_get_raw_data, NULL);
static DEVICE_ATTR(int1_config, 0664, attr_get_intconfig1, attr_set_intconfig1);
static DEVICE_ATTR(int1_duration, 0664, attr_get_duration1, attr_set_duration1);
static DEVICE_ATTR(int1_thresholdx, 0664, attr_get_threshx1, attr_set_threshx1);
static DEVICE_ATTR(int1_thresholdy, 0664, attr_get_threshy1, attr_set_threshy1);
static DEVICE_ATTR(int1_thresholdz, 0664, attr_get_threshz1, attr_set_threshz1);
static DEVICE_ATTR(int1_source, 0444, attr_get_source1, NULL);
static DEVICE_ATTR(run_calibration, 0664, attr_get_eeprom_writing, attr_set_eeprom_writing);
static DEVICE_ATTR(run_fast_calibration, 0664, attr_get_fast_calibration, attr_set_fast_calibration);
static DEVICE_ATTR(cal_offset, 0444, attr_get_cal_offset, NULL);
static DEVICE_ATTR(fast_calibration_x, 0664, attr_get_fast_calibration_x, attr_set_fast_calibration_x);
static DEVICE_ATTR(fast_calibration_y, 0664, attr_get_fast_calibration_y, attr_set_fast_calibration_y);
static DEVICE_ATTR(fast_calibration_z, 0664, attr_get_fast_calibration_z, attr_set_fast_calibration_z);
static DEVICE_ATTR(selftest, 0666, attr_get_selftest, attr_set_selftest);
#ifdef DEBUG
static DEVICE_ATTR(reg_value, 0600, attr_reg_get, attr_reg_set);
static DEVICE_ATTR(reg_addr, 0200, NULL, attr_addr_set);
#endif

static struct attribute *k303b_acc_attributes[] = {
	&dev_attr_enable.attr,
	&dev_attr_poll_delay.attr,
	&dev_attr_range.attr,
	&dev_attr_value.attr,
	&dev_attr_raw.attr,
	&dev_attr_int1_config.attr,
	&dev_attr_int1_duration.attr,
	&dev_attr_int1_thresholdx.attr,
	&dev_attr_int1_thresholdy.attr,
	&dev_attr_int1_thresholdz.attr,
	&dev_attr_int1_source.attr,
	&dev_attr_run_calibration.attr,
	&dev_attr_run_fast_calibration.attr,
	&dev_attr_cal_offset.attr,
	&dev_attr_fast_calibration_x.attr,
	&dev_attr_fast_calibration_y.attr,
	&dev_attr_fast_calibration_z.attr,
	&dev_attr_selftest.attr,
#ifdef DEBUG
	&dev_attr_reg_value.attr,
	&dev_attr_reg_addr.attr,
#endif
	NULL,
};
static const struct attribute_group k303b_acc_attr_group = {
	.attrs = k303b_acc_attributes,
};

#else /* LGE_SENSOR_FACTORY_AAT */
static struct device_attribute attributes[] = {
	__ATTR(enable, 0666, attr_get_enable, attr_set_enable),
	__ATTR(poll_delay, 0666, attr_get_polling_rate, attr_set_polling_rate),

	__ATTR(full_scale, 0666, attr_get_range, attr_set_range),
	__ATTR(int1_config, 0664, attr_get_intconfig1, attr_set_intconfig1),
	__ATTR(int1_duration, 0664, attr_get_duration1, attr_set_duration1),
	__ATTR(int1_thresholdx, 0664, attr_get_threshx1, attr_set_threshx1),
	__ATTR(int1_thresholdy, 0664, attr_get_threshy1, attr_set_threshy1),
	__ATTR(int1_thresholdz, 0664, attr_get_threshz1, attr_set_threshz1),
	__ATTR(int1_source, 0444, attr_get_source1, NULL),
	__ATTR(run_fast_calibration, 0664, attr_fastcal_get, attr_fastcal_set),
	__ATTR(self_test, 0444, attr_get_selftest, NULL),

#ifdef DEBUG
	__ATTR(reg_value, 0600, attr_reg_get, attr_reg_set),
	__ATTR(reg_addr, 0200, NULL, attr_addr_set),
#endif
};

static int create_sysfs_interfaces(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(attributes); i++)
		if (device_create_file(dev, attributes + i))
			goto error;

	return 0;

error:
	for (; i >= 0; i--)
		device_remove_file(dev, attributes + i);
	dev_err(dev, "%s:Unable to create interface\n", __func__);
	return -1;
}

static int remove_sysfs_interfaces(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(attributes); i++)
		device_remove_file(dev, attributes + i);

	return 0;
}
#endif /* LGE_SENSOR_FACTORY_AAT */

static void k303b_acc_input_poll_work_func(struct work_struct *work)
{
	struct k303b_acc_status *stat;

	stat = container_of((struct work_struct *) work,
			struct k303b_acc_status, input_poll_work);

	//mutex_lock(&stat->lock);
	k303b_acc_report_triple(stat);
	//mutex_unlock(&stat->lock);

	if (atomic_read(&stat->enabled))
		hrtimer_start(&stat->hr_timer_poll, stat->polling_ktime, HRTIMER_MODE_REL);
}

enum hrtimer_restart k303b_acc_hr_timer_poll_function(struct hrtimer *timer)
{
	struct k303b_acc_status *stat;

	stat = container_of((struct hrtimer *)timer, struct k303b_acc_status, hr_timer_poll);

	queue_work(stat->hr_timer_poll_work_queue, &stat->input_poll_work);

	return HRTIMER_NORESTART;
}

int k303b_acc_input_open(struct input_dev *input)
{
	struct k303b_acc_status *stat = input_get_drvdata(input);

	dev_dbg(&stat->client->dev, "%s\n", __func__);

	return 0; /*k303b_acc_enable(stat);*/
}

void k303b_acc_input_close(struct input_dev *dev)
{
	struct k303b_acc_status *stat = input_get_drvdata(dev);

	dev_dbg(&stat->client->dev, "%s\n", __func__);
	k303b_acc_disable(stat);
}

static int k303b_acc_validate_pdata(struct k303b_acc_status *stat)
{
	/* checks for correctness of minimal polling period */
		stat->pdata->min_interval =
		max((unsigned int)K303B_ACC_MIN_POLL_PERIOD_MS,
						stat->pdata->min_interval);

	stat->pdata->poll_interval = max(stat->pdata->poll_interval, stat->pdata->min_interval);

	if (stat->pdata->axis_map_x > 2 || stat->pdata->axis_map_y > 2 || stat->pdata->axis_map_z > 2) {
		dev_err(&stat->client->dev, "invalid axis_map value x:%u y:%u z%u\n",
						stat->pdata->axis_map_x,
						stat->pdata->axis_map_y,
						stat->pdata->axis_map_z);
		return -EINVAL;
	}

	/* Only allow 0 and 1 for negation boolean flag */
	if (stat->pdata->negate_x > 1 || stat->pdata->negate_y > 1 || stat->pdata->negate_z > 1) {
		dev_err(&stat->client->dev, "invalid negate value x:%u y:%u z:%u\n",
						stat->pdata->negate_x,
						stat->pdata->negate_y,
						stat->pdata->negate_z);
		return -EINVAL;
	}

	/* Enforce minimum polling interval */
	if (stat->pdata->poll_interval < stat->pdata->min_interval) {
		dev_err(&stat->client->dev, "minimum poll interval violated\n");
		return -EINVAL;
	}

	return 0;
}

static int k303b_acc_input_init(struct k303b_acc_status *stat)
{
	int err;

	INIT_WORK(&stat->input_poll_work, k303b_acc_input_poll_work_func);
	stat->input_dev = input_allocate_device();
	if (!stat->input_dev) {
		err = -ENOMEM;
		dev_err(&stat->client->dev, "input device allocation failed\n");
		goto err0;
	}

	stat->input_dev->open = k303b_acc_input_open;
	stat->input_dev->close = k303b_acc_input_close;

	stat->input_dev->name = K303B_ACC_DEVICE_CUSTOM_NAME;

#if 1 /* LGE_SENSOR_FACTORY_AAT */
	stat->input_dev->dev.init_name = LGE_ACCELEROMETER_NAME;
#endif
    //stat->input_dev->id.bustype = BUS_I2C;
    //stat->input_dev->dev.parent = &stat->client->dev;

	input_set_drvdata(stat->input_dev, stat);

	set_bit(EV_ABS, stat->input_dev->evbit);

	/*	next is used for interruptA sources data if the case */
	set_bit(ABS_MISC, stat->input_dev->absbit);
	/*	next is used for interruptB sources data if the case */
	set_bit(ABS_WHEEL, stat->input_dev->absbit);

	input_set_abs_params(stat->input_dev, ABS_X, G_MIN, G_MAX, FUZZ, FLAT);
	input_set_abs_params(stat->input_dev, ABS_Y, G_MIN, G_MAX, FUZZ, FLAT);
	input_set_abs_params(stat->input_dev, ABS_Z, G_MIN, G_MAX, FUZZ, FLAT);

	/*	next is used for interruptA sources data if the case */
	input_set_abs_params(stat->input_dev, ABS_MISC, INT_MIN, INT_MAX, 0, 0);
	/*	next is used for interruptB sources data if the case */
	input_set_abs_params(stat->input_dev, ABS_WHEEL, INT_MIN, INT_MAX, 0, 0);

	err = input_register_device(stat->input_dev);
	if (err) {
		dev_err(&stat->client->dev, "unable to register input device %s\n", stat->input_dev->name);
		goto err1;
	}

	return 0;

err1:
	input_free_device(stat->input_dev);
err0:
	return err;
}

static void k303b_acc_input_cleanup(struct k303b_acc_status *stat)
{
	input_unregister_device(stat->input_dev);
	input_free_device(stat->input_dev);
}

static int k303b_acc_probe(struct i2c_client *client, const struct i2c_device_id *id)
{

	struct k303b_acc_status *stat;

	u32 smbus_func = (I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA | I2C_FUNC_SMBUS_I2C_BLOCK);

	int err = -1;

	dev_info(&client->dev, "probe start.\n");

	stat = kzalloc(sizeof(struct k303b_acc_status), GFP_KERNEL);
	if (stat == NULL) {
		err = -ENOMEM;
		dev_err(&client->dev,
				"failed to allocate memory for module data: %d\n", err);
		goto exit_check_functionality_failed;
	}

	/* Support for both I2C and SMBUS adapter interfaces. */
	stat->use_smbus = 0;
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_warn(&client->dev, "client not i2c capable\n");
		if (i2c_check_functionality(client->adapter, smbus_func)) {
			stat->use_smbus = 1;
			dev_warn(&client->dev, "client using SMBUS\n");
		} else {
			err = -ENODEV;
			dev_err(&client->dev, "client nor SMBUS capable\n");
            goto err_free_data;
		}
	}

	mutex_init(&stat->lock);
	mutex_lock(&stat->lock);

	stat->client = client;
	i2c_set_clientdata(client, stat);

	stat->pdata = kmalloc(sizeof(*stat->pdata), GFP_KERNEL);
	if (stat->pdata == NULL) {
		err = -ENOMEM;
		dev_err(&client->dev, "failed to allocate memory for pdata: %d\n", err);
		goto err_mutexunlock;
	}

	if(client->dev.of_node) {
		/* device tree type */
		err = k303b_parse_dt(&client->dev,stat->pdata);

	} else {
		if (client->dev.platform_data == NULL) {
			default_k303b_acc_pdata.gpio_int1 = int1_gpio;

			memcpy(stat->pdata, &default_k303b_acc_pdata, sizeof(*stat->pdata));
			dev_info(&client->dev, "using default plaform_data\n");
		} else {
			memcpy(stat->pdata, client->dev.platform_data, sizeof(*stat->pdata));
		}
	}
	
	stat->pdata->power_on = k303b_power_on;
	stat->pdata->power_off = k303b_power_off;
	
	stat->hr_timer_poll_work_queue = 0;

	/* ADD ST */
	if (stat->pdata->gpio_int1 >= 0) {
	    err = k303b_gpio_config(stat);
	    if (err) {
		    pr_info("k303b_gpio_config ERROR  = %d\n", err);
	    }
    }

	err = k303b_acc_validate_pdata(stat);
	if (err < 0) {
		dev_err(&client->dev, "failed to validate platform data\n");
		goto exit_kfree_pdata;
	}

/*
	if (stat->pdata->init) {
		err = stat->pdata->init();
		if (err < 0) {
			dev_err(&client->dev, "init failed: %d\n", err);
			goto err_pdata_init;
		}

	}
*/
	if (stat->pdata->gpio_int1 >= 0) {
		stat->irq1 = gpio_to_irq(stat->pdata->gpio_int1);
		pr_info("%s: %s has set irq1 to irq: %d, mapped on gpio:%d\n",
			K303B_ACC_DEV_NAME, __func__, stat->irq1, stat->pdata->gpio_int1);
	}

	k303b_acc_set_init_register_values(stat);

#ifdef K303B_ACCEL_CALIBRATION
	stat->cal_data.bCalLoaded = 0;
#endif
    mdelay(30); //delay for the first i2c communication
	err = k303b_acc_device_power_on(stat);
	if (err < 0) {
		dev_err(&client->dev, "power on failed: %d\n", err);
		goto err_pdata_init;
	}

	atomic_set(&stat->enabled, 1);
#if 0
	err = k303b_acc_update_fs_range(stat, stat->pdata->fs_range);
	if (err < 0) {
		dev_err(&client->dev, "update_fs_range failed\n");
		goto  err_power_off;
	}

	err = k303b_acc_update_odr(stat, stat->pdata->poll_interval);
	if (err < 0) {
		dev_err(&client->dev, "update_odr failed\n");
		goto  err_power_off;
	}
#endif

	stat->hr_timer_poll_work_queue =
			create_workqueue("k303b_acc_hr_timer_poll_wq");
	if(!stat->hr_timer_poll_work_queue) {
		err = -ENOMEM;
		goto err_remove_hr_work_queue;
	}

	hrtimer_init(&stat->hr_timer_poll, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	stat->hr_timer_poll.function = &k303b_acc_hr_timer_poll_function;

	err = k303b_acc_input_init(stat);
	if (err < 0) {
		dev_err(&client->dev, "input init failed\n");
		goto err_remove_hr_work_queue;
	}

#if 1 /* LGE_SENSOR_FACTORY_AAT */
	/* Register sysfs hooks */
	err = sysfs_create_group(&stat->input_dev->dev.kobj, &k303b_acc_attr_group);
#else
	err = create_sysfs_interfaces(&client->dev);
#endif
	if (err < 0) {
		dev_err(&client->dev,
		   "device %s sysfs register failed\n", K303B_ACC_DEV_NAME);
		goto err_input_cleanup;
	}


	k303b_acc_device_power_off(stat);

	/* As default, do not report information */
	atomic_set(&stat->enabled, 0);

	if (stat->pdata->gpio_int1 >= 0) {
		INIT_WORK(&stat->irq1_work, k303b_acc_irq1_work_func);
		stat->irq1_work_queue = create_singlethread_workqueue("k303b_acc_wq1");
		if (!stat->irq1_work_queue) {
			err = -ENOMEM;
			dev_err(&client->dev, "cannot create work queue1: %d\n", err);
			goto err_remove_sysfs_int;
		}
		err = request_irq(stat->irq1, k303b_acc_isr1, IRQF_TRIGGER_RISING, "k303b_acc_irq1", stat);
		if (err < 0) {
			dev_err(&client->dev, "request irq1 failed: %d\n", err);
			goto err_destoyworkqueue1;
		}
		disable_irq_nosync(stat->irq1);
	}

	mutex_unlock(&stat->lock);

	dev_info(&client->dev, "%s: probed\n", K303B_ACC_DEV_NAME);

	return 0;

err_destoyworkqueue1:
	if (stat->pdata->gpio_int1 >= 0)
		destroy_workqueue(stat->irq1_work_queue);
err_remove_sysfs_int:
#if 1 /* LGE_SENSOR_FACTORY_AAT */
	sysfs_remove_group(&stat->input_dev->dev.kobj, &k303b_acc_attr_group);
#else
	remove_sysfs_interfaces(&client->dev);
#endif
err_input_cleanup:
	k303b_acc_input_cleanup(stat);
err_remove_hr_work_queue:
	if (stat->hr_timer_poll_work_queue) {
			flush_workqueue(stat->hr_timer_poll_work_queue);
			destroy_workqueue(stat->hr_timer_poll_work_queue);
	}
//err_power_off:
	k303b_acc_device_power_off(stat);
err_pdata_init:
/*	if (stat->pdata->exit) remove this code to prevent kernel crash when probe failed.
		stat->pdata->exit();*/
exit_kfree_pdata:
	kfree(stat->pdata);
err_mutexunlock:
	mutex_unlock(&stat->lock);
err_free_data:
    kfree(stat);
exit_check_functionality_failed:
	pr_err("%s: Driver Init failed\n", K303B_ACC_DEV_NAME);
	return err;
}

static int k303b_acc_remove(struct i2c_client *client)
{

	struct k303b_acc_status *stat = i2c_get_clientdata(client);

	dev_info(&stat->client->dev, "driver removing\n");

	if (stat->pdata->gpio_int1 >= 0) {
		free_irq(stat->irq1, stat);
		gpio_free(stat->pdata->gpio_int1);
		destroy_workqueue(stat->irq1_work_queue);
	}

	k303b_acc_disable(stat);
	k303b_acc_input_cleanup(stat);

#if 1 /* LGE_SENSOR_FACTORY_AAT */
	sysfs_remove_group(&stat->input_dev->dev.kobj, &k303b_acc_attr_group);
#else
	remove_sysfs_interfaces(&client->dev);
#endif
	if (stat->hr_timer_poll_work_queue) {
			flush_workqueue(stat->hr_timer_poll_work_queue);
			destroy_workqueue(stat->hr_timer_poll_work_queue);
	}

/*	if (stat->pdata->exit)
		stat->pdata->exit();*/
	kfree(stat->pdata);
	kfree(stat);

	return 0;
}

#ifdef CONFIG_PM
static int k303b_acc_resume(struct i2c_client *client)
{
	struct k303b_acc_status *stat = i2c_get_clientdata(client);

	if (stat->on_before_suspend)
		return k303b_acc_enable(stat);

	return 0;
}

static int k303b_acc_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct k303b_acc_status *stat = i2c_get_clientdata(client);

	stat->on_before_suspend = atomic_read(&stat->enabled);

	return k303b_acc_disable(stat);
}
#else
#define k303b_acc_suspend	NULL
#define k303b_acc_resume	NULL
#endif /* CONFIG_PM */

static const struct i2c_device_id k303b_acc_id[] = {
		{ K303B_ACC_DEV_NAME, 0 },
		{ },
};

MODULE_DEVICE_TABLE(i2c, k303b_acc_id);
#ifdef CONFIG_OF
static struct of_device_id k303b_acc_match_table[] = {
	    { .compatible = "st,k303b_acc",},
		{ },
};
#else
#define k303b_acc_match_table NULL
#endif

static struct i2c_driver k303b_acc_driver = {
	.driver = {
			.owner = THIS_MODULE,
			.name = K303B_ACC_DEV_NAME,
			.of_match_table = k303b_acc_match_table,
	},
	.probe = k303b_acc_probe,
	.remove = k303b_acc_remove,
	.suspend = k303b_acc_suspend,
	.resume = k303b_acc_resume,
	.id_table = k303b_acc_id,
};

static int __init k303b_acc_init(void)
{
	pr_info("%s accelerometer driver: init\n", K303B_ACC_DEV_NAME);
	return i2c_add_driver(&k303b_acc_driver);
}

static void __exit k303b_acc_exit(void)
{
	i2c_del_driver(&k303b_acc_driver);
}

module_init(k303b_acc_init);
module_exit(k303b_acc_exit);

MODULE_DESCRIPTION("k303b accelerometer driver");
MODULE_AUTHOR("Matteo Dameno, Denis Ciocca, STMicroelectronics");
MODULE_LICENSE("GPL");

