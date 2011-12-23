/* DVB USB framework compliant Linux driver for the SKNET MonsterTV HD ISDB-T
 * receiver. (Version 1 Frontend)
 *
 * Copyright (c) 2009 Gombei Nanashi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "mtvhd.h"
#include "dvb_math.h"

/*
 * ISDB TS encryption                     : ASIE5606(B)
 * Tuner Module (OFDM demodulator + PLL)  : BTD-VK482Z
 */

/* Private data per frontend */
struct mtvhd_fe_state {
	struct dvb_frontend frontend;
	struct dvb_usb_adapter *adap;
	u32	frequency;
	fe_status_t stat;
};

/* Read/Write register of demodulator */
static int mtvhd_demod_reg_read(struct dvb_frontend *fe, u8 reg)
{
	struct mtvhd_fe_state *st = fe->demodulator_priv;
	int ret, retry;

	for (retry = 0; retry < 2; retry ++) {
		ret = mtvhd_i2c_read_byte(st->adap->dev, MTVHD_I2C_DEMOD, reg);
		if (ret != -EREMOTEIO) {
			break;
		}
		msleep(50);
	}

	return ret;
}

static int mtvhd_demod_reg_write(struct dvb_frontend *fe, u8 reg, u8 val)
{
	struct mtvhd_fe_state *st = fe->demodulator_priv;
	int ret, retry;

	for (retry = 0; retry < 2; retry ++) {
		ret = mtvhd_i2c_write_byte(st->adap->dev, MTVHD_I2C_DEMOD, reg, val);
		if (ret != -EREMOTEIO) {
			break;
		}
		msleep(50);
	}

	return ret;
}

/* Indirect register write to tuner via demodulator's 2nd I2C */
static int mtvhd_pll_reg_write(struct dvb_frontend *fe, u16 div, u8 band)
{
	struct mtvhd_fe_state *st = fe->demodulator_priv;
	int ret, retry;
	u8 data[7];

	data[0] = MTVHD_DEMOD_2ND_I2C;
	data[1] = MTVHD_I2C_PLL;
	data[2] = (u8)(div >> 8);	/* Divider high */
	data[3] = (u8)(div & 0xFF);	/* Divider low */
	data[4] = 0x91;			/* Control */
	data[5] = band;			/* Bandwidth */
	data[6] = 0xE3;			/* AGC control ? */

	for (retry = 0; retry < 2; retry ++) {
		ret = mtvhd_i2c_write_block(st->adap->dev, MTVHD_I2C_DEMOD, data, 7);
		if (ret != -EREMOTEIO) {
			break;
		}
		msleep(50);
	}

	return ret;
}

/* Read/Write register of ASIE560x */
static int mtvhd_asie560x_reg_read(struct dvb_usb_adapter *adap, u8 reg)
{
	struct mtvhd_adapter_state *st = adap->priv;
	int ret, retry;

	if (st->asie560x_addr == 0) {
		return -EUNATCH;
	}
	for (retry = 0; retry < 2; retry ++) {
		ret = mtvhd_i2c_read_byte(adap->dev, st->asie560x_addr, reg);
		if (ret != -EREMOTEIO) {
			break;
		}
		msleep(50);
	}

	return ret;
}

static int mtvhd_asie560x_probe(struct dvb_usb_adapter *adap, u8 reg)
{
	struct mtvhd_adapter_state *st = adap->priv;
	int ret, retry;
	u8 i2c_addr;

	if (st->asie560x_addr == 0) {
		/* First access: determine I2C address for ASIE560x */

		for (retry = 0; retry < 2; retry ++) {
			i2c_addr = MTVHD_I2C_ASIE5606_OLD;
			ret = mtvhd_i2c_read_byte(adap->dev, i2c_addr, reg);
			if (ret == -EREMOTEIO) {
				/* try alternate I2C address */
				i2c_addr = MTVHD_I2C_ASIE5606;
				ret = mtvhd_i2c_read_byte(adap->dev, i2c_addr, reg);
			}
			if (ret >= 0) {
				/* set I2C address */
				st->asie560x_addr = i2c_addr;
				break;
			}
			else if (ret != -EREMOTEIO) {
				break;
			}
			msleep(50);
		}
	}
	else {
		ret = mtvhd_asie560x_reg_read(adap, reg);
	}

	return ret;
}

static int mtvhd_asie560x_reg_write(struct dvb_usb_adapter *adap, u8 reg, u8 val)
{
	struct mtvhd_adapter_state *st = adap->priv;
	int ret, retry;

	if (st->asie560x_addr == 0) {
		return -EUNATCH;
	}
	for (retry = 0; retry < 2; retry ++) {
		ret = mtvhd_i2c_write_byte(adap->dev, st->asie560x_addr, reg, val);
		if (ret != -EREMOTEIO) {
			break;
		}
		msleep(50);
	}

	return ret;
}

/* ASIE5606 power off/on */
static int mtvhd_asie560x_power_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	struct mtvhd_adapter_state *st = adap->priv;
	int ret = 0;

	if (onoff) {	/* power on */
		deb_info("ASIE560x[%d] power on\n", adap->id);
		ret = mtvhd_asv5211_gpio(adap->dev, 0x11, 0xFF);
		msleep(100);
	}
	else {	/* power off */
		deb_info("ASIE560x[%d] power off\n", adap->id);
		if (st->asie560x_addr != 0) {
			mtvhd_asie560x_reg_write(adap, 0x80, 0x00);
			mtvhd_asie560x_reg_write(adap, 0x04, 0x00);
			mtvhd_asie560x_reg_write(adap, 0x00, 0x00);
		}
		/* The chip is powered off in mtvhd_v1_power_ctrl() */
	}

	return (ret < 0) ? ret : 0;
}

/* Power Control */
int mtvhd_v1_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	int ret = 0;
	u8 buff[0x3a];

	if (onoff) {	/* power on */
		deb_info("Power on\n");
		if (d->state == DVB_USB_STATE_INIT) {
#if 0
			/* Read EEPROM data */
			ret = mtvhd_ctrl_msg(d, buff, 0x0C, 0x0000, 0x0000, 0x3A);
			/* ASV5211 status ? */
			ret = mtvhd_ctrl_msg(d, buff, 0x17, 0x0002, 0x0000, 0x03);
#endif
			/* Initialize GPIO (Initial power state) */
			ret = mtvhd_asv5211_gpio(d, 0xFB, 0x07);
			/* ? */
			ret = mtvhd_ctrl_msg(d, buff, 0x10, 0x0200, 0x0000, 0x01);
			msleep(50);
		}
		/* Ensure to power off B-CAS ? */
		ret = mtvhd_asv5211_gpio(d, 0x01, 0x00);
		msleep(100);
		/* Power on tuner ? & Assert Reset ? */
		ret = mtvhd_asv5211_gpio(d, 0xDD, 0x0C);
		msleep(100);

		/* Power off */
		ret = mtvhd_asv5211_gpio(d, 0xD5, 0x00);
		msleep(50);

		/* Deassert Reset ? */
		ret = mtvhd_asv5211_gpio(d, 0x08, 0x00);
		/* Power on tuner ? */
		ret = mtvhd_asv5211_gpio(d, 0x40, 0x40);
		msleep(5);
		ret = mtvhd_asv5211_gpio(d, 0x80, 0x80);
		msleep(50);
		ret = mtvhd_asv5211_gpio(d, 0x04, 0x04);
		msleep(50);
	}
	else {	/* power off */
		deb_info("Power off\n");
		msleep(100);
		/* Power off B-CAS ? */
		ret = mtvhd_asv5211_gpio(d, 0x01, 0x00);
		msleep(100);
		/* Power off & Assert Reset ? */
		ret = mtvhd_asv5211_gpio(d, 0xDD, 0x0C);
		msleep(100);
	}

	return (ret < 0) ? ret : 0;
}

/* Initialization code of Demodulator registers */
static struct mtvhd_reg_data demod_init_code[] = {
	/* addr, data */
	{0x01, 0x00}, {0x02, 0x00}, {0x03, 0x00}, {0x04, 0x00},
	{0x05, 0x00}, {0x06, 0x00}, {0x07, 0x00}, {0x08, 0x00},
	{0x0C, 0x00}, {0x11, 0x21}, {0x12, 0x0B}, {0x13, 0x2C},
	{0x14, 0x0A}, {0x15, 0x40}, {0x16, 0x0A}, {0x17, 0x70},
	{0x18, 0x31}, {0x19, 0x13}, {0x1A, 0x31}, {0x1B, 0x13},
	{0x1C, 0x2A}, {0x1D, 0xAA}, {0x1E, 0xA2}, {0x1F, 0x08},
	{0x20, 0x00}, {0x21, 0xFF}, {0x22, 0x80}, {0x23, 0x4C},
	{0x24, 0x4C}, {0x25, 0x00}, {0x26, 0x00}, {0x27, 0x0C},
	{0x28, 0x60}, {0x29, 0x6B}, {0x2A, 0x40}, {0x2B, 0x40},
	{0x2C, 0xFF}, {0x2D, 0x00}, {0x2E, 0xFF}, {0x2F, 0x00},
	{0x30, 0x28}, {0x31, 0x0F}, {0x32, 0xA0}, {0x34, 0x3F},
	{0x38, 0x01}, {0x39, 0x70}, {0x3A, 0x2D}, {0x3B, 0x21},
	{0x3C, 0x3F}, {0x3D, 0x10}, {0x3E, 0x08}, {0x3F, 0x0C},
	{0x40, 0x0C}, {0x41, 0x00}, {0x42, 0x00}, {0x43, 0x4F},
	{0x44, 0xFF}, {0x46, 0x20}, {0x47, 0x00}, {0x48, 0x90},
	{0x49, 0xE6}, {0x4A, 0x02}, {0x4B, 0x54}, {0x4C, 0x00},
	{0x50, 0x00}, {0x51, 0x58}, {0x52, 0x20}, {0x54, 0x57},
	{0x55, 0xF1}, {0x56, 0x20}, {0x57, 0x70}, {0x58, 0x60},
	{0x5C, 0x50}, {0x5D, 0x00}, {0x5F, 0x80},
	{0x70, 0x18}, {0x71, 0x00}, {0x72, 0x00}, {0x75, 0x22},
	{0x76, 0x00}, {0x77, 0x01}, {0x7C, 0x00}, {0x7D, 0x52},
	{0xBA, 0x00}, {0xBB, 0x00}, {0xBC, 0x00}, {0xC2, 0x10},
	{0xC7, 0x00}, {0xE4, 0x1A}, {0xE9, 0x08},
	{0xEC, 0x00}, {0xEF, 0x00},
};

static int mtvhd_tuner_init(struct dvb_frontend *fe)
{
	int i, ret;

	deb_info("Tuner init\n");

	/* Initialize demodulator */
	for (i = 0; i < ARRAY_SIZE(demod_init_code); i++) {
		ret = mtvhd_demod_reg_write(fe,
				demod_init_code[i].addr,
				demod_init_code[i].data);
		if (ret != 0) {
			break;
		}
	}

	return ret;
}

static int mtvhd_freq_set(struct dvb_frontend *fe, u32 freq)
{
	int ret;
	u16 div;
	u8 band;

	deb_info("Frequency set %d -> %d\n", fe->demodulator_priv->frequency, freq);

	/* PLL clock: 142.857kHz ? */
	div = 399 + (freq / 142857);	/* ((freq / 1000000) + 57) * 7 */
	if (freq < 470000000) {	/* original: freq < 420000000 ??? */
		band = 0x82;	/* CATV ? */
	}
	else {
		band = 0x84;	/* UHF ? */
	}
	ret = mtvhd_pll_reg_write(fe, div, band);
	if (ret != 0) {
		return ret;
	}

	/* post-setting on demodulator */
	ret = mtvhd_demod_reg_write(fe, 0x01, 0x40);

	return ret;
}

/* Initialize ASIE560x */
static int mtvhd_asie560x_init(struct dvb_usb_adapter *adap)
{
	struct mtvhd_adapter_state *st = adap->priv;
	int ret;
	u8 reg, crypto_mode;

	/* ASIE560x Power On */
	ret = mtvhd_asie560x_power_ctrl(adap, 1);
	if (ret != 0) {
		err("ASIE560x power on failed: %d", ret);
		return ret;
	}

	deb_info("ASIE560x[%d] init\n", adap->id);

	/* Probe ASIE560x and set crypto mode */
	crypto_mode = 0;
	ret = mtvhd_asie560x_probe(adap, 0x09);
	if (ret < 0) {
		err("ASIE560x probe failed: %d", ret);
		return ret;
	}
	switch (ret & 0x07) {
	  case 0x04:
		crypto_mode = MTVHD_CRYPTO_XOR;
		break;

	  case 0x06:
		crypto_mode = MTVHD_CRYPTO_DES;
		break;

	  default:
		err("Unknown crypto mode - ASIE560x reg 9: %02x", ret);
	}
	ret = mtvhd_crypto_init(st, crypto_mode);
	if (ret != 0) {
		err("Failed to initialize crypto library (%02x): %d", crypto_mode, ret);
		/* continue without decryption */
	}

	ret = mtvhd_asie560x_reg_write(adap, 0x05, crypto_mode);
	if (ret != 0) {
		err("ASIE560x crypto mode setting failed: %d", ret);
		return ret;
	}
	/* Set crypto key on ASIE5606 (all 0) */
	for (reg = 0x10; reg < 0x20; reg++) {
		ret = mtvhd_asie560x_reg_write(adap, reg, 0x00);
		if (ret != 0) {
			err("ASIE560x crypto key setting failed: %d", ret);
			return ret;
		}
	}

	return ret;
}


/* --- Frontend API I/F --- */

static int mtvhd_fe_read_status(struct dvb_frontend *fe,
					fe_status_t *status)
{
	int ret;

	deb_info("FE read status: ");

	*status = 0;
	ret = mtvhd_demod_reg_read(fe, 0x80);
	deb_info("%02x -> ", ret);
	if (ret >= 0) {
		/* FIXME: I don't know actual meaning of each bit */
		if ((ret & 0xA8) == 0) {
			*status = FE_HAS_SIGNAL
			        | FE_HAS_CARRIER
			        | FE_HAS_VITERBI
			        | FE_HAS_SYNC
			        | FE_HAS_LOCK;
		}
		ret = 0;
	}
	deb_info("%02x\n", *status);

	return ret;
}

static int mtvhd_fe_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	int ret;
	u32 val, x;

	deb_info("FE read snr: ");

	*snr = 0;
	ret = mtvhd_demod_reg_read(fe, 0x8B);
	if (ret < 0) {
		goto done;
	}
	val = ret << 16;
	ret = mtvhd_demod_reg_read(fe, 0x8C);
	if (ret < 0) {
		goto done;
	}
	val |= ret << 8;
	ret = mtvhd_demod_reg_read(fe, 0x8D);
	if (ret < 0) {
		goto done;
	}
	val |= ret;
	ret = 0;
	deb_info("%06x -> ", val);

	if (val == 0 || val > 0x540000) {
		goto done;
	}

	x = 20 * intlog10(0x540000 / val);
	*snr = (u16)(x >> 16);

done:
	deb_info("%04x (%d.%d dB)\n", *snr, *snr >> 8, ((*snr & 0xFF) * 10) >> 8);

	return ret;
}

static int mtvhd_fe_read_signal_strength(struct dvb_frontend *fe,
						u16 *strength)
{
	deb_info("FE read signal strength: ");

	*strength = 0;
	/* FIXME: tentatively return S/N */
	return mtvhd_fe_read_snr(fe, strength);
}

static int mtvhd_fe_get_tune_settings(struct dvb_frontend *fe,
				struct dvb_frontend_tune_settings *tune)
{
	deb_info("FE get tune settings\n");

	tune->min_delay_ms = 800;
#if 0
	tune->step_size = fe->ops.info.frequency_stepsize;	/* 1/7 MHz */
	tune->max_drift = (fe->ops.info.frequency_stepsize * 2) + 1;
#endif

	return 0;
}

static int mtvhd_fe_set_frontend(struct dvb_frontend* fe,
				struct dvb_frontend_parameters *fep)
{
	struct mtvhd_fe_state *st = fe->demodulator_priv;
	int ret;

	deb_info("FE set frontend\n");

	/* for recovery from DTV_CLEAR */
	fe->dtv_property_cache.delivery_system = SYS_ISDBT;

	/* NOTE: this driver ignores all parameters but frequency. */
	ret = mtvhd_freq_set(fe, fep->frequency);
	if (ret != 0) {
		err("Tuner frequency setting failed: %d", ret);
		return ret;
	}

	/* Initialize PID filter */
	ret = mtvhd_pid_filter_init(st->adap);
	if (ret != 0) {
		err("PID filter setting failed: %d", ret);
	}

	return ret;
}

static int mtvhd_fe_get_frontend(struct dvb_frontend* fe,
				struct dvb_frontend_parameters *fep)
{
//	struct mtvhd_fe_state *st = fe->demodulator_priv;

	deb_info("FE get frontend\n");

	fep->u.ofdm.bandwidth = BANDWIDTH_6_MHZ;
	return 0;
}

static int mtvhd_fe_set_property(struct dvb_frontend* fe,
				struct dtv_property *tvp)
{
	int ret = 0;

	deb_info("FE set property (cmd = %x)\n", tvp->cmd);

	switch (tvp->cmd) {
	  case DTV_DELIVERY_SYSTEM:
		if (tvp->u.data != SYS_ISDBT) {
			ret = -EINVAL;
		}
		break;

	  case DTV_CLEAR:
	  case DTV_TUNE:
	  case DTV_FREQUENCY:
		break;

	  default:
		ret = -EINVAL;
	}

	return ret;
}

static int mtvhd_fe_get_property(struct dvb_frontend* fe,
				struct dtv_property *tvp)
{
	deb_info("FE get property (cmd = %x)\n", tvp->cmd);

	/* for recovery from DTV_CLEAR */
	fe->dtv_property_cache.delivery_system = SYS_ISDBT;

	return 0;
}

/* for debug ? */
static int mtvhd_fe_write(struct dvb_frontend *fe, const u8 data[], int len)
{
	struct mtvhd_fe_state *st = fe->demodulator_priv;
	int i, ret;

	deb_info("FE write\n");

	if (len < 2 || data[0] + len >= MTVHD_DEMOD_2ND_I2C) {
		return -EINVAL;
	}

	ret = 0;
	if (data[0] == MTVHD_DEMOD_2ND_I2C) {
		/* Indirect access to tuner: no check & no retry */
		ret = mtvhd_i2c_write_block(st->adap->dev, MTVHD_I2C_DEMOD, data, len);
	}
	else {
		for (i = 0; i < len - 1; i++) {
			ret = mtvhd_demod_reg_write(fe, data[0] + i, data[i + 1]);
			if (ret != 0) {
				break;
			}
		}
	}

	return ret;
}

/* Wake up adapter */
static int mtvhd_fe_init(struct dvb_frontend *fe)
{
	struct mtvhd_fe_state *st = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;

	deb_info("FE init\n");

	ret = mtvhd_tuner_init(fe);
	if (ret != 0) {
		err("Tuner initialization failed: %d", ret);
		return ret;
	}

	ret = mtvhd_asie560x_init(st->adap);
	if (ret != 0) {
		err("ASIE560x initialization failed: %d", ret);
		return ret;
	}
	c->delivery_system = SYS_ISDBT;

	if (c->frequency != 0) {
		/* Retrieve previous frequency setting */
		ret = mtvhd_freq_set(fe, c->frequency);
		if (ret != 0) {
			err("Tuner frequency setting failed: %d", ret);
			return ret;
		}

		/* Initialize PID filter */
		ret = mtvhd_pid_filter_init(st->adap);
		if (ret != 0) {
			err("PID filter setting failed: %d", ret);
			return ret;
		}
	}

	return ret;
}

/* Sleep adapter */
static int mtvhd_fe_sleep(struct dvb_frontend *fe)
{
	struct mtvhd_fe_state *st = fe->demodulator_priv;

	deb_info("FE sleep\n");

	return mtvhd_asie560x_power_ctrl(st->adap, 0);
}

static void mtvhd_fe_release(struct dvb_frontend* fe)
{
	struct mtvhd_fe_state *st = fe->demodulator_priv;

	deb_info("FE release\n");

	mtvhd_crypto_release(st->adap->priv);
	kfree(fe->demodulator_priv);
	fe->demodulator_priv = NULL;
}

static struct dvb_frontend_ops mtvhd_v1_fe_ops;

static struct dvb_frontend *mtvhd_v1_fe_attach(struct dvb_usb_adapter *adap)
{
	struct mtvhd_fe_state *st;

	st = kzalloc(sizeof(struct mtvhd_fe_state), GFP_KERNEL);
	if (st == NULL) {
		return NULL;
	}

	deb_info("attaching frontend %s\n", mtvhd_v1_fe_ops.info.name);
	memcpy(&st->frontend.ops, &mtvhd_v1_fe_ops,
			sizeof(struct dvb_frontend_ops));
	st->frontend.demodulator_priv = st;
	st->adap = adap;

	return &st->frontend;
}

int mtvhd_v1_frontend_attach(struct dvb_usb_adapter *adap)
{
	adap->fe = mtvhd_v1_fe_attach(adap);
	if (adap->fe == NULL) {
		return -ENOMEM;
	}

	/* Hook streaming complete process */
	return mtvhd_stream_init(adap);
}

static struct dvb_frontend_ops mtvhd_v1_fe_ops = {
	.info = {
		.name			= "SKNET MonsterTV HD V1 ISDB-T",
		.type			= FE_OFDM,
		.frequency_min		= 110000000,	/* CABLE C13ch, center */
		.frequency_max		= 770000000,	/* UHF 62ch, upper */
		.frequency_stepsize	= 142857,	/* 1MHz / 7 */

		/* NOTE: this driver ignores all parameters but frequency. */
		.caps = FE_CAN_INVERSION_AUTO | FE_CAN_FEC_AUTO |
#if 0
			FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_4_5 | FE_CAN_FEC_5_6 | FE_CAN_FEC_6_7 |
			FE_CAN_FEC_7_8 | FE_CAN_FEC_8_9 |
			FE_CAN_QAM_16 | FE_CAN_QAM_64 |
#endif
			FE_CAN_QAM_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_BANDWIDTH_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO |
			FE_CAN_HIERARCHY_AUTO,
	},

	.release		= mtvhd_fe_release,
	.init			= mtvhd_fe_init,
	.sleep			= mtvhd_fe_sleep,
	.write			= mtvhd_fe_write,
	.set_property		= mtvhd_fe_set_property,
	.get_property		= mtvhd_fe_get_property,
	.set_frontend		= mtvhd_fe_set_frontend,
	.get_frontend		= mtvhd_fe_get_frontend,
	.get_tune_settings	= mtvhd_fe_get_tune_settings,
	.read_status		= mtvhd_fe_read_status,
	.read_signal_strength	= mtvhd_fe_read_signal_strength,
	.read_snr		= mtvhd_fe_read_snr,
};
