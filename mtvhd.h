/* Common header-file of the Linux driver for the SKNET MonsterTV HD ISDB-T
 * receiver.
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
#ifndef _DVB_USB_MTVHD_H_
#define _DVB_USB_MTVHD_H_

#define DVB_USB_LOG_PREFIX "mtvhd"
#include "dvb-usb.h"
#include "dvb_filter.h"

/* debug */
extern int dvb_usb_mtvhd_debug;
#define deb_err(args...)	dprintk(dvb_usb_mtvhd_debug, 0x01, args)
#define deb_info(args...)	dprintk(dvb_usb_mtvhd_debug, 0x02, args)
#define deb_xfer(args...)	dprintk(dvb_usb_mtvhd_debug, 0x04, args)
#define deb_rc(args...)		dprintk(dvb_usb_mtvhd_debug, 0x08, args)

/* MonsterTV HD specific definitions */
/* I2C slave address */
#define MTVHD_I2C_EEPROM	0xA8
#define MTVHD_I2C_ASIE5606_OLD	0x5A	/* for adapter 0 */
#define MTVHD_I2C_ASIE5607	0x5A	/* for adapter 0 */
#define MTVHD_I2C_ASIE5607_1	0x5E	/* for adapter 1 */
#define MTVHD_I2C_ASIE5606	0x4A	/* for adapter 0 */
#define MTVHD_I2C_ASIE5606_1	0x4E	/* for adapter 1 */
#define MTVHD_I2C_DEMOD		0x20	/* Demodulator for adapter 0 */
#define MTVHD_I2C_DEMOD_1	0x30	/* Demodulator for adapter 1 */
#define MTVHD_I2C_PLL		0xC0	/* PLL for adapter 0 */
#define MTVHD_I2C_TUNER		0xC0	/* Tuner for adapter 0 */
#define MTVHD_I2C_TUNER_1	0xC4	/* Tuner for adapter 1 */

#define MTVHD_DEMOD_2ND_I2C	0xFE	/* Indirect I2C access */

#define MTVHD_CRYPTO_XOR	0x91
#define MTVHD_CRYPTO_DES	0x80
#define MTVHD_CRYPTO_AESXXX	0x30	/* ASIE5607 */

#define MTVHD_TYPE_PCI		0x0001

//#define MTVHD_MAX_PID_FILTERS	2

/* ASV5211 EEPROM data structure */
struct mtvhd_eeprom_data {
	u8	magic_h;		/* 0xAE */
	u8	magic_l;		/* 0x36 */
	u8	vendor_h;
	u8	vendor_l;
	u8	product_id_h;
	u8	product_id_l;
	u8	str_desc10[10];
	u8	str_desc20[16];
	u8	str_desc40[15];
	u8	pading[9];
	u8	flag;
};
#define MTVHD_EEPROM_FLAG_SELF_POWER	0x80
#define MTVHD_EEPROM_FLAG_SINGLE_IF	0x40


/* Private data per device */
struct mtvhd_device_state {
	u16	type;
#ifdef CONFIG_DVB_USB_MTVHD_REMOTE_CONTROL
	u8	rc_data[33];
	u8	rc_index;
#endif
};

/* Private data per adapter */
struct mtvhd_adapter_state {
	void	(*complete_orig)(struct usb_data_stream *, u8 *, size_t);
	void	(*decrypt)(void *, u8 *);
	void	*crypto_ctx;	/* Context for crypto library */
	u8	crypto_mode;	/* Encryption mode (XOR or DES) */
	u8	asie560x_addr;	/* I2C address for ASIE5606 */
	u8	pad0;
	u8	buff_used;	/* Number of valid bytes in buffer */
	u8	packet_buff[TS_SIZE];	/* TS Packet buffer */
#if 0
	u8	pid_enable;	/* Enable PID filter */
	u16	pid[MTVHD_MAX_PID_FILTERS];	/* PID filter */
#endif
};

/* Register address/data pair */
struct mtvhd_reg_data {
	u8	addr;
	u8	data;
};

/* Common USB control */
extern int mtvhd_ctrl_msg(struct dvb_usb_device *d, void *data,
				u8 req, u16 value, u16 index, u16 size);

/* Common I2C control */
extern int mtvhd_i2c_read_byte(struct dvb_usb_device *d,
				u8 i2c_addr, u8 cmd);
extern int mtvhd_i2c_write_byte(struct dvb_usb_device *d,
				u8 i2c_addr, u8 cmd, u8 val);
extern int mtvhd_i2c_write_block(struct dvb_usb_device *d,
				u8 i2c_addr, u8 *data, int len);

/* Common ASV5211 control */
extern int mtvhd_asv5211_gpio(struct dvb_usb_device *d, u8 mask, u8 val);
extern int mtvhd_pid_filter_init(struct dvb_usb_adapter *adap);

#ifdef CONFIG_DVB_USB_MTVHD_V2
/* Version 2 (HDU2/HDUC/HDP2) specific functions */
extern int mtvhd_v2_power_ctrl(struct dvb_usb_device *d, int onoff);
extern int mtvhd_v2_frontend_attach(struct dvb_usb_adapter *adap);
#endif

#ifdef CONFIG_DVB_USB_MTVHD_V1
/* Version 1 (HDUS/HDP) specific functions */
extern int mtvhd_v1_power_ctrl(struct dvb_usb_device *d, int onoff);
extern int mtvhd_v1_frontend_attach(struct dvb_usb_adapter *adap);
#endif

/* Stream data decryption */
int mtvhd_stream_init(struct dvb_usb_adapter *adap);
extern int mtvhd_crypto_init(struct mtvhd_adapter_state *st, int mode);
extern void mtvhd_crypto_release(struct mtvhd_adapter_state *st);
extern int mtvhd_des_init(struct mtvhd_adapter_state *st);
extern void mtvhd_des_release(struct mtvhd_adapter_state *st);
extern void mtvhd_des_decrypt(void *crypto_ctx, u8 *packet);
#ifdef CONFIG_DVB_USB_MTVHD_V1
extern void mtvhd_xor_decrypt(void *crypto_ctx, u8 *packet);
#endif

#endif /* _DVB_USB_MTVHD_H_ */
