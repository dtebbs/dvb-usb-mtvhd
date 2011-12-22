/* DVB USB framework compliant Linux driver for the SKNET MonsterTV HD ISDB-T
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
#include "mtvhd.h"

/* module parameters */
int dvb_usb_mtvhd_debug;
module_param_named(debug, dvb_usb_mtvhd_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=err,2=info,4=xfer,8=rc)." DVB_USB_DEBUG_STATUS);

#ifdef CONFIG_DVB_USB_MTVHD_REMOTE_CONTROL
static int hdp_rc;
module_param_named(enable_hdp_rc, hdp_rc, bool, 0644);
MODULE_PARM_DESC(enable_hdp_rc, "enable remote controller on PCI type board.");
#endif

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

/* USB Control Message */
int mtvhd_ctrl_msg(struct dvb_usb_device *d, void *data,
				u8 req, u16 value, u16 index, u16 size)
{
	int ret;

	ret = mutex_lock_interruptible(&d->usb_mutex);
	if (ret != 0) {
		return ret;
	}

	deb_xfer("USB control msg: c0 %02x %04x %04x %04x >>> ",
			req, value, index, size);

	ret = usb_control_msg(d->udev, usb_rcvctrlpipe(d->udev, 0), req,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			value, index, data, size, 2000);

	mutex_unlock(&d->usb_mutex);
	if (ret != size) {
		deb_err("USB control message error (req:%02x val:%04x idx:%04x ret:%04x) >>> ",
			req, value, index, ret);
		debug_dump(((u8 *)data), ret, deb_err);
		return -EIO;
	}

	debug_dump(((u8 *)data), size, deb_xfer);

	return 0;
}

/* I2C operation */
/* SMBus read byte */
int mtvhd_i2c_read_byte(struct dvb_usb_device *d,
				u8 i2c_addr, u8 cmd)
{
	int ret;
	u8 buff[2];

	ret = mtvhd_ctrl_msg(d, buff, 0x02, ((u16)cmd << 8) | i2c_addr, 0x0000, 0x02);
	if (ret != 0) {
		return ret;
	}
	if (buff[0] != 1) {	/* status */
		return -EREMOTEIO;
	}
	return buff[1];
}

/* SMBus write byte */
int mtvhd_i2c_write_byte(struct dvb_usb_device *d,
				u8 i2c_addr, u8 cmd, u8 val)
{
	int ret;
	u8 buff[2];

	ret = mtvhd_ctrl_msg(d, buff, 0x03, ((u16)cmd << 8) | i2c_addr, val, 0x02);
	if (ret != 0) {
		return ret;
	}
	if (buff[0] != 1) {	/* status */
		return -EREMOTEIO;
	}
	return ret;
}

/* I2C block write */
int mtvhd_i2c_write_block(struct dvb_usb_device *d,
				u8 i2c_addr, u8 *data, int len)
{
	int ret, n, count;
	u8 buff[33];

	if (len > 32) {
		return -EINVAL;
	}

	ret = mutex_lock_interruptible(&d->usb_mutex);
	if (ret != 0) {
		return ret;
	}

	deb_xfer("I2C write block: addr:%02x len:%d >>> ", i2c_addr, len);

	n = 0;
	count = 0;
	while (len > n) {
		int size;
		u8 d1, d2;

		size = (len - n) >= 3 ? 3 : len - n;
		d1 = ((size > 1) ? data[1] : 0);
		d2 = ((size > 2) ? data[2] : 0);

		/* Buffering I2C packet data (up to 3 bytes at a time) */
		count = usb_control_msg(d->udev, usb_rcvctrlpipe(d->udev, 0),
			0x0D, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			((u16)data[0] << 8) | n, ((u16)d2 << 8) | d1,
			buff, 1 + size, 2000);
		if (count != 1 + size || buff[0] != 1) {
			ret = -EIO;
			break;
		}
		n += size;
		data += size;
	}
	if (ret == 0) {
		/* Kick I2C transfer */
		count = usb_control_msg(d->udev, usb_rcvctrlpipe(d->udev, 0),
			0x0E, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			i2c_addr, 0x0000, buff, 1 + len, 2000);
		if (count != 1 + len) {
			ret = -EIO;
		}
		else if (buff[0] != 1) {
			ret = -EREMOTEIO;
		}
	}

	mutex_unlock(&d->usb_mutex);
	if (ret != 0) {
		deb_err("I2C write block error (addr:%02x offset:%02x count:%02x ret:%d) >>> ",
			i2c_addr, n, count, ret);
		debug_dump(buff, count, deb_err);
		return ret;
	}

	debug_dump(buff, 1 + len, deb_xfer);

	return 0;
}

/* ASV5211 GPIO */
int mtvhd_asv5211_gpio(struct dvb_usb_device *d, u8 mask, u8 val)
{
	int ret;
	u8 buff[1];

	ret = mtvhd_ctrl_msg(d, buff, 0x08, (mask << 8) | val, 0x0000, 0x01);
	if (ret == 0) {
		ret = buff[0];
	}

	return ret;
}

/* Read/Write register of asv5211 PID filter */
int mtvhd_pid_filter_read_byte(struct dvb_usb_adapter *adap, u8 reg)
{
	int ret;
	u8 buff[2];

	if (adap->id == 1) {
		reg += 0x80;
	}
	ret = mtvhd_ctrl_msg(adap->dev, buff, 0x04, reg, 0x0000, 2);
	if (ret != 0) {
		return ret;
	}
	if (buff[0] != 1) {
		return -EREMOTEIO;
	}

	return buff[1];
}

int mtvhd_pid_filter_read_word(struct dvb_usb_adapter *adap, u8 reg)
{
	int ret;
	u16 x;
	u8 buff[3];

	if (adap->id == 1) {
		reg += 0x80;
	}
	ret = mtvhd_ctrl_msg(adap->dev, buff, 0x04, reg, 0x0000, 3);
	if (ret != 0) {
		return ret;
	}
	if (buff[0] != 1) {
		return -EREMOTEIO;
	}

	x = ((u16)buff[1] << 8) + buff[2];	/* Big Endian */
	/* Do not use be16_to_cpup() due to misalignment */

	return x;
}

int mtvhd_pid_filter_write_byte(struct dvb_usb_adapter *adap, u8 reg, u8 val)
{
	int ret;
	u16 d0;
	u8 buff[2];

	d0 = (u16)val << 8;
	if (adap->id == 1) {
		reg += 0x80;
	}
	ret = mtvhd_ctrl_msg(adap->dev, buff, 0x05, (reg | d0), 0x0000, 2);
	if (ret == 0 && buff[0] != 1) {
		ret = -EREMOTEIO;
	}

	return ret;
}

int mtvhd_pid_filter_write_word(struct dvb_usb_adapter *adap, u8 reg, u16 val)
{
	int ret;
	u16 d0, d1;
	u8 buff[3];

	d0 = (val & 0xFF00);
	d1 = (val & 0x00FF);
	if (adap->id == 1) {
		reg += 0x80;
	}
	ret = mtvhd_ctrl_msg(adap->dev, buff, 0x05, (reg | d0), d1, 3);
	if (ret == 0 && buff[0] != 1) {
		ret = -EREMOTEIO;
	}

	return ret;
}

/* Reset stream controller ? */
int mtvhd_stream_reset(struct dvb_usb_adapter *adap)
{
	int ret;
	u8 buff[1];

	ret = mtvhd_ctrl_msg(adap->dev, buff, 0x09, 0x0100 | adap->id,
				0x0000, 0x0001);
	if (ret == 0 && buff[0] != 1) {
		ret = -EREMOTEIO;
	}

	return ret;
}

/* Initialize PID filter */
int mtvhd_pid_filter_init(struct dvb_usb_adapter *adap)
{
	int ret;
#if 0
	struct mtvhd_adapter_state *st = adap->priv;
	u8 x;
#endif

	ret = mtvhd_stream_reset(adap);
	if (ret != 0){
		return ret;
	}

#if 0
	/* Check if USB (ASV5211) is high speed */
	mtvhd_ctrl_msg(adap->dev, buf, 0x0A, 0x0100, 0x0000, 0x01);
	msleep(30);
#endif

	ret = mtvhd_pid_filter_read_byte(adap, 0x40);
	if (ret < 0) {
		return ret;
	}
#if 0
	x = (u8)ret;
	if (st->crypto_mode == MTVHD_CRYPTO_XOR) {
		ret = mtvhd_pid_filter_write_word(adap, 0x3F, 0x0000 + (x | 0x04));
		ret = mtvhd_pid_filter_write_byte(adap, 0x40, 0x0F);
		ret = mtvhd_pid_filter_read_byte(adap, 0x40);
		x = (u8)ret;
		ret = mtvhd_pid_filter_write_byte(adap, 0x40, (x | 0x03));
	}
	else {
		ret = mtvhd_pid_filter_write_byte(adap, 0x40, (x | 0x0F));
	}
#else
	ret = mtvhd_pid_filter_write_byte(adap, 0x40, 0x0F);
#endif
	if (ret != 0) {
		return ret;
	}
	ret = mtvhd_pid_filter_write_word(adap, 0x41, 0x1FFF);
	if (ret != 0) {
		return ret;
	}
	ret = mtvhd_pid_filter_write_word(adap, 0x43, 0x1FFF);

	return ret;
}
#if 0
static int mtvhd_pid_filter_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	struct mtvhd_adapter_state *st = adap->priv;

	st->pid_enable = onoff ? 1 : 0;
}

static int mtvhd_pid_filter(struct dvb_usb_adapter *adap,
				int index, u16 pid, int onoff)
{
	struct mtvhd_adapter_state *st = adap->priv;

	st->pid[index] = onoff ? pid : 0;
}
#endif

/* Streaming Control */
static int mtvhd_stream_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	struct dvb_usb_device *d = adap->dev;
	struct mtvhd_adapter_state *st = adap->priv;
	int ret, ret2;
	u8 buff[1];

	if (onoff) {	/* streaming on */
		deb_info("Streaming[%d] on\n", adap->id);
		ret = mtvhd_ctrl_msg(d, buff, 0x06, adap->id, 0x0000, 0x0001);
	}
	else {		/* streaming off */
		deb_info("Streaming[%d] off\n", adap->id);
		ret = mtvhd_ctrl_msg(d, buff, 0x07, adap->id, 0x0000, 0x0001);
		ret2 = mtvhd_stream_reset(adap);
		if (ret == 0) {
			ret = ret2;
		}
	}
	st->buff_used = 0;

	return ret;
}

#ifdef CONFIG_DVB_USB_MTVHD_REMOTE_CONTROL
/* Initialize remote controller */
static int mtvhd_rc_init(struct dvb_usb_device *d)
{
	u8 buff[1];

	return mtvhd_ctrl_msg(d, buff, 0x01, 0x000B, 0x0000, 0x01);
}

/* Remote Controller key mapping */
struct dvb_usb_rc_key mtvhd_rc_keys[] = {
	{ 0x0a80, KEY_SCREEN },			/* Gamen hyoji */
	{ 0x0a81, KEY_POWER },			/* POWER */
	{ 0x0a82, KEY_MUTE },			/* MUTE */
	{ 0x0a83, KEY_VOLUMEUP },		/* VOL UP */
	{ 0x0a84, KEY_VOLUMEDOWN },		/* VOL DOWN */
	{ 0x0a85, KEY_CHANNELUP },		/* CH UP */
	{ 0x0a86, KEY_CHANNELDOWN },		/* CH DOWN */
	{ 0x0a91, KEY_1 },			/* 1 */
	{ 0x0a92, KEY_2 },			/* 2 */
	{ 0x0a93, KEY_3 },			/* 3 */
	{ 0x0a94, KEY_4 },			/* 4 */
	{ 0x0a95, KEY_5 },			/* 5 */
	{ 0x0a96, KEY_6 },			/* 6 */
	{ 0x0a97, KEY_7 },			/* 7 */
	{ 0x0a98, KEY_8 },			/* 8 */
	{ 0x0a99, KEY_9 },			/* 9 */
	{ 0x0a90, KEY_0 },			/* 10 */
	{ 0x0ac0, KEY_F11 },			/* 11 */
	{ 0x0ac1, KEY_F12 },			/* 12 */
	{ 0x0ac2, KEY_MENU },			/* MENU */
	{ 0x0ac3, KEY_ZOOM },			/* FULL SCREEN */
	{ 0x0ac4, KEY_TEXT },			/* Jimaku */
	{ 0x0ac5, KEY_AUDIO },			/* Onsei Kirikae */
	{ 0x0ac6, KEY_LEFT },			/* LEFT */
	{ 0x0ac7, KEY_UP },			/* UP */
	{ 0x0ac8, KEY_RIGHT },			/* RIGHT */
	{ 0x0ac9, KEY_DOWN },			/* DOWN */
	{ 0x0aca, KEY_OK },			/* Kettei */
	{ 0x0acb, KEY_EPG },			/* EPG */
	{ 0x0acc, KEY_PREVIOUS },		/* Modoru */
	{ 0x0acd, KEY_RECORD },			/* RECORD */
	{ 0x0ace, KEY_MEMO },			/* MEMO */
	{ 0x0ad0, KEY_STOP },			/* STOP */
	{ 0x0ad1, KEY_PLAY },			/* PLAY */
	{ 0x0ad2, KEY_PAUSE },			/* PAUSE */
	{ 0x0ad3, KEY_FIRST },			/* |<< */
	{ 0x0ad4, KEY_BACK },			/* << */
	{ 0x0ad5, KEY_FORWARD },		/* >> */
	{ 0x0ad6, KEY_LAST },			/* >>| */
	{ 0x0ad7, KEY_BOOKMARKS },		/* Shiori */
	{ 0x0ad8, KEY_GOTO },			/* JUMP */
	{ 0x0ada, KEY_BLUE },			/* BLUE */
	{ 0x0adb, KEY_RED },			/* RED */
	{ 0x0adc, KEY_GREEN },			/* GREEN */
	{ 0x0add, KEY_YELLOW },			/* YELLOW */

#if 0
	/* Toshiba TV Remote Controller */
	{ 0x4001, KEY_1 },			/* 1 */
	{ 0x4002, KEY_2 },			/* 2 */
	{ 0x4003, KEY_3 },			/* 3 */
	{ 0x4004, KEY_4 },			/* 4 */
	{ 0x4005, KEY_5 },			/* 5 */
	{ 0x4006, KEY_6 },			/* 6 */
	{ 0x4007, KEY_7 },			/* 7 */
	{ 0x4008, KEY_8 },			/* 8 */
	{ 0x4009, KEY_9 },			/* 9 */
	{ 0x400a, KEY_0 },			/* 10 */
	{ 0x400b, KEY_F11 },			/* 11 */
	{ 0x400c, KEY_F12 },			/* 12 */
	{ 0x4012, KEY_POWER },			/* POWER */
	{ 0x4010, KEY_MUTE },			/* MUTE */
	{ 0x401a, KEY_VOLUMEUP },		/* VOL UP */
	{ 0x401e, KEY_VOLUMEDOWN },		/* VOL DOWN */
	{ 0x401b, KEY_CHANNELUP },		/* CH UP */
	{ 0x401f, KEY_CHANNELDOWN },		/* CH DOWN */
	{ 0x403d, KEY_OK },			/* Kettei */
	{ 0x403e, KEY_UP },			/* UP */
	{ 0x403f, KEY_DOWN },			/* DOWN */
	{ 0x405f, KEY_LEFT },			/* LEFT */
	{ 0x405b, KEY_RIGHT },			/* RIGHT */
	{ 0x4020, KEY_PAGEUP },			/* UP */
	{ 0x4021, KEY_PAGEDOWN }.		/* DOWN */
	{ 0x4022, KEY_FRAMEBACK },		/* LEFT */
	{ 0x4023, KEY_FRAMEFORWARD },		/* RIGHT */
	{ 0x402b, KEY_STOP },			/* STOP */
	{ 0x402d, KEY_PLAYPAUSE },		/* PLAY/PAUSE */
	{ 0x4027, KEY_FIRST },			/* |<< */
	{ 0x402c, KEY_BACK },			/* << */
	{ 0x402e, KEY_FORWARD },		/* >> */
	{ 0x4026, KEY_LAST },			/* >>| */
	{ 0x4013, KEY_AUDIO },			/* Onsei Kirikae */
	{ 0x403b, KEY_PREVIOUS },		/* Modoru */
	{ 0x403c, KEY_EXIT },			/* Shuryou */
	{ 0x4052, KEY_TEXT },			/* Jimaku */
	{ 0x4027, KEY_MENU },			/* Quick */
	{ 0x4034, KEY_PROGRAM },		/* REGZA Link */
	{ 0x406e, KEY_EPG },			/* Bangumihyo */
	{ 0x4073, KEY_BLUE },			/* BLUE */
	{ 0x4074, KEY_RED },			/* RED */
	{ 0x4075, KEY_GREEN },			/* GREEN */
	{ 0x4076, KEY_YELLOW },			/* YELLOW */
	{ 0x400f, KEY_VIDEO_NEXT },		/* Nyuryoku Kirikae + */
	{ 0x403a, KEY_VIDEO_PREV },		/* Nyuryoku Kirikae - */
	{ 0x4029, KEY_SWITCHVIDEOMODE },	/* 2-Gamen */
	{ 0x402b, KEY_ZOOM },			/* Gamen Size */
	{ 0x4029, KEY_TITLE },			/* Gamen Hyoji */
	{ 0x4071, KEY_INFO },			/* Bangumi Setsumei */
	{ 0x4077, KEY_PROGRAM },		/* Mini Bangumihyo */
	{ 0x4025, KEY_WWW },			/* Broadband */
	{ 0x40d0, KEY_SETUP },			/* Settei */
	{ 0x4050, KEY_PAUSE },			/* Seishi */
#endif
};

/*
 * FORMAT:
 * 
 * data[32]:            count (MAX: 8)
 * data[0] - data[3]:   first event
 * data[4] - data[7]:   second event (if available)
 * ...
 * data[28] - data[31]: eighth event (if available)
 * 
 * data[n]:   checksum for data[n+1]
 * data[n+1]: actual key part
 * data[n+2]: checksum for data[n+3]
 * data[n+3]: vendor/custom part of the key
 */

/* Poll remote controller state & convert to input event code */
int mtvhd_rc_query(struct dvb_usb_device *d, u32 *event, int *state)
{
	int ret;
	struct mtvhd_device_state *st = d->priv;
	u8 key[5];

	/* Initialize as no event */
	*event = 0;
	*state = REMOTE_NO_KEY_PRESSED;

	if ((st->type & MTVHD_TYPE_PCI) && !hdp_rc) {
		/* Disabled remote controller on HDP */
		return 0;
	}

	/*
	 * Since rc_query function returns only single event,
	 * keep the 2nd (or later) event and respond it at next polling time.
	 */
	if (st->rc_index == 0 || st->rc_index >= st->rc_data[32]) {
		/* rc_data[] buffer is empty: retrieve from the remote controler */
		ret = mtvhd_ctrl_msg(d, st->rc_data, 0x00, 0, 0, 33);

		st->rc_index = 0;
		if (ret != 0) {
			st->rc_data[32] = 0;
			return ret;
		}
		if (st->rc_data[32] == 0) {
			/* No event */
			return 0;
		}
		deb_rc("rc_key (%d): ", st->rc_data[32]);
		if (st->rc_data[32] > 8) {
			/* Exceed max number of events */
			st->rc_data[32] = 8;
		}
		debug_dump(st->rc_data, st->rc_data[32] * 4, deb_rc);
	}

	/* Convert byte order to meet universal NEC remote processor key */
	key[0] = 0x01;			/* DVB_USB_RC_NEC_KEY_PRESSED */
	key[1] = st->rc_data[st->rc_index * 4 + 3];
	key[2] = st->rc_data[st->rc_index * 4 + 2];
	key[3] = st->rc_data[st->rc_index * 4 + 1];
	key[4] = st->rc_data[st->rc_index * 4 + 0];
	st->rc_index++;

	/* call the universal NEC remote processor, to find out the key's state and event */
	dvb_usb_nec_rc_key_to_event(d, key, event, state);

	return 0;
}
#endif /* CONFIG_DVB_USB_MTVHD_REMOTE_CONTROL */

/* USB Driver stuff */
#ifdef CONFIG_DVB_USB_MTVHD_V1
static struct dvb_usb_device_properties hdus_properties;
#endif
#ifdef CONFIG_DVB_USB_MTVHD_V2
static struct dvb_usb_device_properties hdu2_properties;
static struct dvb_usb_device_properties hduc_properties;
#endif
#ifdef CONFIG_DVB_USB_MTVHD_REMOTE_CONTROL
#ifdef CONFIG_DVB_USB_MTVHD_V1
static struct dvb_usb_device_properties hdp_properties;
#endif
#ifdef CONFIG_DVB_USB_MTVHD_V2
static struct dvb_usb_device_properties hdp2_properties;
static struct dvb_usb_device_properties hdpg_properties;
#endif
#endif

static int mtvhd_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	int ret = -ENODEV;
	struct dvb_usb_device *d = NULL;
	struct mtvhd_device_state *st;

	/* interface 0 is used by ISDB-T receiver and
	   interface 1 is for remote controller (HID) */
	if (intf->cur_altsetting->desc.bInterfaceNumber == 0) {
#ifdef CONFIG_DVB_USB_MTVHD_REMOTE_CONTROL
		if (!hdp_rc) {
			/* try PCI (no remote control) type first */
#ifdef CONFIG_DVB_USB_MTVHD_V2
			ret = dvb_usb_device_init(intf, &hdp2_properties,
					THIS_MODULE, &d, adapter_nr);
			if (ret == -ENODEV) {
				ret = dvb_usb_device_init(intf, &hdpg_properties,
					THIS_MODULE, &d, adapter_nr);
			}
#endif
#ifdef CONFIG_DVB_USB_MTVHD_V1
			if (ret == -ENODEV) {
				ret = dvb_usb_device_init(intf, &hdp_properties,
					THIS_MODULE, &d, adapter_nr);
			}
#endif
		}
#endif
#ifdef CONFIG_DVB_USB_MTVHD_V2
		if (ret == -ENODEV) {
			ret = dvb_usb_device_init(intf, &hdu2_properties,
				THIS_MODULE, &d, adapter_nr);
		}
		if (ret == -ENODEV) {
			ret = dvb_usb_device_init(intf, &hduc_properties,
				THIS_MODULE, &d, adapter_nr);
		}
#endif
#ifdef CONFIG_DVB_USB_MTVHD_V1
		if (ret == -ENODEV) {
			ret = dvb_usb_device_init(intf, &hdus_properties,
				THIS_MODULE, &d, adapter_nr);
		}
#endif

		if (ret == 0 && d != NULL) {	/* warm state */
			st = d->priv;
			switch (id->idProduct) {
			  case USB_PID_SKNET_MONSTERTV_HDP:
			  case USB_PID_SKNET_MONSTERTV_HDP2:
			  case USB_PID_SKNET_MONSTERTV_HDP_GOLD:
			  case USB_PID_SKNET_MONSTERTV_HDP2_GOLD:
			  case USB_PID_SKNET_MONSTERTV_HDPS:
			  case USB_PID_SKNET_MONSTERTV_HDP2W:
				st->type |= MTVHD_TYPE_PCI;
				break;

			  default:
				;	/* Nothing to do */
			}
			mtvhd_rc_init(d);
		}
		return ret;
	}
	/* remote controller is configured while initialization of interface 0:
	   nothing to do for interface 1 */

	return -ENODEV;
}

/* do not change the order of the ID table */
static struct usb_device_id mtvhd_table [] = {
/* 00 */	{ USB_DEVICE(USB_VID_VIDZMEDIA, USB_PID_SKNET_MONSTERTV_HDUS) },
/* 01 */	{ USB_DEVICE(USB_VID_VIDZMEDIA, USB_PID_QUIXUN_QRS_NT100P) },
/* 02 */	{ USB_DEVICE(USB_VID_VIDZMEDIA, USB_PID_LOGITEC_LDT_FS100U) },
/* 03 */	{ USB_DEVICE(USB_VID_VIDZMEDIA, USB_PID_SKNET_MONSTERTV_HDP) },
/* 04 */	{ USB_DEVICE(USB_VID_VIDZMEDIA, USB_PID_SKNET_MONSTERTV_HDP2) },
/* 05 */	{ USB_DEVICE(USB_VID_VIDZMEDIA, USB_PID_SKNET_MONSTERTV_HDU2) },
/* 06 */	{ USB_DEVICE(USB_VID_VIDZMEDIA, USB_PID_QUIXUN_QRS_UT100B) },
/* 07 */	{ USB_DEVICE(USB_VID_VIDZMEDIA, USB_PID_SKNET_MONSTERTV_HDUC) },
/* 08 */	{ USB_DEVICE(USB_VID_VIDZMEDIA, USB_PID_SKNET_MONSTERTV_HDP_GOLD) },
/* 09 */	{ USB_DEVICE(USB_VID_VIDZMEDIA, USB_PID_SKNET_MONSTERTV_HDP2_GOLD) },
/* 10 */	{ USB_DEVICE(USB_VID_VIDZMEDIA, USB_PID_SKNET_MONSTERTV_HDUC_GOLD) },
/* 11 */	{ USB_DEVICE(USB_VID_VIDZMEDIA, USB_PID_SKNET_MONSTERTV_HDP2W) },
#if 0
/* 12 */	{ USB_DEVICE(USB_VID_VIDZMEDIA, USB_PID_SKNET_MONSTERTV_HDPS) },
/* 13 */	{ USB_DEVICE(USB_VID_VIDZMEDIA, USB_PID_SKNET_MONSTERTV_HDU2S) },
#endif
		{ 0 }		/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, mtvhd_table);

#ifdef CONFIG_DVB_USB_MTVHD_V2
/* HDU2 / HDP2 : (ASIE5606 + TC90507XBG + SMT-MJ101) x 2 */
static struct dvb_usb_device_properties hdu2_properties = {
	.caps = 0,
	.size_of_priv = sizeof(struct mtvhd_device_state),

	.num_adapters = 2,
	.adapter = {
		{
#if 0
			.caps = DVB_USB_ADAP_HAS_PID_FILTER |
				DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,

			.pid_filter_count = 8,
			.pid_filter       = mtvhd_pid_filter,
			.pid_filter_ctrl  = mtvhd_pid_filter_ctrl,
#endif
			.frontend_attach  = mtvhd_v2_frontend_attach,
			.streaming_ctrl   = mtvhd_stream_ctrl,
			.stream = {
				.type = USB_BULK,
				.count = 8,
				.endpoint = 0x81,
				.u = {
					.bulk = {
						.buffersize = 0x10000,
					}
				}
			},
			.size_of_priv = sizeof(struct mtvhd_adapter_state),
		},
		{
#if 0
			.caps = DVB_USB_ADAP_HAS_PID_FILTER |
				DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,

			.pid_filter_count = 8,
			.pid_filter       = mtvhd_pid_filter,
			.pid_filter_ctrl  = mtvhd_pid_filter_ctrl,
#endif
			.frontend_attach  = mtvhd_v2_frontend_attach,
			.streaming_ctrl   = mtvhd_stream_ctrl,
			.stream = {
				.type = USB_BULK,
				.count = 8,
				.endpoint = 0x82,
				.u = {
					.bulk = {
						.buffersize = 0x10000,
					}
				}
			},
			.size_of_priv = sizeof(struct mtvhd_adapter_state),
		}
	},

	.power_ctrl = mtvhd_v2_power_ctrl,

#ifdef CONFIG_DVB_USB_MTVHD_REMOTE_CONTROL
	.rc_interval      = 200,
	.rc_key_map       = mtvhd_rc_keys,
	.rc_key_map_size  = ARRAY_SIZE(mtvhd_rc_keys),
	.rc_query         = mtvhd_rc_query,
#endif

	.num_device_descs = 4,
	.devices = {
		{ .name = "SKNET MonsterTV HDP2 ISDB-T PCI",
		  .cold_ids = { NULL },
		  .warm_ids = { &mtvhd_table[4], NULL },
		},
		{ .name = "SKNET MonsterTV HDU2 ISDB-T USB2.0",
		  .cold_ids = { NULL },
		  .warm_ids = { &mtvhd_table[5], NULL },
		},
		{ .name = "SKNET MonsterTV HDP2 Gold ISDB-T PCI",
		  .cold_ids = { NULL },
		  .warm_ids = { &mtvhd_table[9], NULL },
		},
		{ .name = "SKNET MonsterTV HDP2W ISDB-T PCI",
		  .cold_ids = { NULL },
		  .warm_ids = { &mtvhd_table[11], NULL },
		},
	}
};

#ifdef CONFIG_DVB_USB_MTVHD_REMOTE_CONTROL
/* HDP2 : (ASIE5606 + TC90507XBG + SMT-MJ101) x 2  PCI (no remote control) */
static struct dvb_usb_device_properties hdp2_properties = {
	.caps = 0,
	.size_of_priv = sizeof(struct mtvhd_device_state),

	.num_adapters = 2,
	.adapter = {
		{
#if 0
			.caps = DVB_USB_ADAP_HAS_PID_FILTER |
				DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,

			.pid_filter_count = 8,
			.pid_filter       = mtvhd_pid_filter,
			.pid_filter_ctrl  = mtvhd_pid_filter_ctrl,
#endif
			.frontend_attach  = mtvhd_v2_frontend_attach,
			.streaming_ctrl   = mtvhd_stream_ctrl,
			.stream = {
				.type = USB_BULK,
				.count = 8,
				.endpoint = 0x81,
				.u = {
					.bulk = {
						.buffersize = 0x10000,
					}
				}
			},
			.size_of_priv = sizeof(struct mtvhd_adapter_state),
		},
		{
#if 0
			.caps = DVB_USB_ADAP_HAS_PID_FILTER |
				DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,

			.pid_filter_count = 8,
			.pid_filter       = mtvhd_pid_filter,
			.pid_filter_ctrl  = mtvhd_pid_filter_ctrl,
#endif
			.frontend_attach  = mtvhd_v2_frontend_attach,
			.streaming_ctrl   = mtvhd_stream_ctrl,
			.stream = {
				.type = USB_BULK,
				.count = 8,
				.endpoint = 0x82,
				.u = {
					.bulk = {
						.buffersize = 0x10000,
					}
				}
			},
			.size_of_priv = sizeof(struct mtvhd_adapter_state),
		}
	},

	.power_ctrl = mtvhd_v2_power_ctrl,

	.num_device_descs = 3,
	.devices = {
		{ .name = "SKNET MonsterTV HDP2 ISDB-T PCI",
		  .cold_ids = { NULL },
		  .warm_ids = { &mtvhd_table[4], NULL },
		},
		{ .name = "SKNET MonsterTV HDP2 Gold ISDB-T PCI",
		  .cold_ids = { NULL },
		  .warm_ids = { &mtvhd_table[9], NULL },
		},
		{ .name = "SKNET MonsterTV HDP2W ISDB-T PCI",
		  .cold_ids = { NULL },
		  .warm_ids = { &mtvhd_table[11], NULL },
		},
	}
};
#endif /* CONFIG_DVB_USB_MTVHD_REMOTE_CONTROL */

/* HDUC / HDP Gold : (ASIE5606 + TC90507XBG + SMT-MJ101) x 1 */
static struct dvb_usb_device_properties hduc_properties = {
	.caps = 0,
	.size_of_priv = sizeof(struct mtvhd_device_state),

	.num_adapters = 1,
	.adapter = {
		{
#if 0
			.caps = DVB_USB_ADAP_HAS_PID_FILTER |
				DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,

			.pid_filter_count = 8,
			.pid_filter       = mtvhd_pid_filter,
			.pid_filter_ctrl  = mtvhd_pid_filter_ctrl,
#endif
			.frontend_attach  = mtvhd_v2_frontend_attach,
			.streaming_ctrl   = mtvhd_stream_ctrl,
			.stream = {
				.type = USB_BULK,
				.count = 8,
				.endpoint = 0x81,
				.u = {
					.bulk = {
						.buffersize = 0x10000,
					}
				}
			},
			.size_of_priv = sizeof(struct mtvhd_adapter_state),
		},
	},

	.power_ctrl = mtvhd_v2_power_ctrl,

#ifdef CONFIG_DVB_USB_MTVHD_REMOTE_CONTROL
	.rc_interval      = 200,
	.rc_key_map       = mtvhd_rc_keys,
	.rc_key_map_size  = ARRAY_SIZE(mtvhd_rc_keys),
	.rc_query         = mtvhd_rc_query,
#endif

	.num_device_descs = 5,
	.devices = {
		{ .name = "Logitec LDT_FS100U ISDB-T USB2.0",
		  .cold_ids = { NULL },
		  .warm_ids = { &mtvhd_table[2], NULL },
		},
		{ .name = "Quixun QRS_UT100B ISDB-T USB2.0",
		  .cold_ids = { NULL },
		  .warm_ids = { &mtvhd_table[6], NULL },
		},
		{ .name = "SKNET MonsterTV HDUC ISDB-T USB2.0",
		  .cold_ids = { NULL },
		  .warm_ids = { &mtvhd_table[7], NULL },
		},
		{ .name = "SKNET MonsterTV HDP Gold ISDB-T PCI",
		  .cold_ids = { NULL },
		  .warm_ids = { &mtvhd_table[8], NULL },
		},
		{ .name = "SKNET MonsterTV HDUC Gold ISDB-T PCI",
		  .cold_ids = { NULL },
		  .warm_ids = { &mtvhd_table[10], NULL },
		},
	}
};

#ifdef CONFIG_DVB_USB_MTVHD_REMOTE_CONTROL
/* HDP Gold : (ASIE5606 + TC90507XBG + SMT-MJ101) x 1  PCI (no remote control) */
static struct dvb_usb_device_properties hdpg_properties = {
	.caps = 0,
	.size_of_priv = sizeof(struct mtvhd_device_state),

	.num_adapters = 1,
	.adapter = {
		{
#if 0
			.caps = DVB_USB_ADAP_HAS_PID_FILTER |
				DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,

			.pid_filter_count = 8,
			.pid_filter       = mtvhd_pid_filter,
			.pid_filter_ctrl  = mtvhd_pid_filter_ctrl,
#endif
			.frontend_attach  = mtvhd_v2_frontend_attach,
			.streaming_ctrl   = mtvhd_stream_ctrl,
			.stream = {
				.type = USB_BULK,
				.count = 8,
				.endpoint = 0x81,
				.u = {
					.bulk = {
						.buffersize = 0x10000,
					}
				}
			},
			.size_of_priv = sizeof(struct mtvhd_adapter_state),
		},
	},

	.power_ctrl = mtvhd_v2_power_ctrl,

	.num_device_descs = 1,
	.devices = {
		{ .name = "SKNET MonsterTV HDP Gold ISDB-T PCI",
		  .cold_ids = { NULL },
		  .warm_ids = { &mtvhd_table[8], NULL },
		},
	}
};
#endif /* CONFIG_DVB_USB_MTVHD_REMOTE_CONTROL */
#endif /* CONFIG_DVB_USB_MTVHD_V2 */

#ifdef CONFIG_DVB_USB_MTVHD_V1
/* HDUS(F) / HDP : (ASIE5606 + xx90507) x 1 */
static struct dvb_usb_device_properties hdus_properties = {
	.caps = 0,
	.size_of_priv = sizeof(struct mtvhd_device_state),

	.num_adapters = 1,
	.adapter = {
		{
#if 0
			.caps = DVB_USB_ADAP_HAS_PID_FILTER |
				DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,

			.pid_filter_count = 8,
			.pid_filter       = mtvhd_pid_filter,
			.pid_filter_ctrl  = mtvhd_pid_filter_ctrl,
#endif
			.frontend_attach  = mtvhd_v1_frontend_attach,
			.streaming_ctrl   = mtvhd_stream_ctrl,
			.stream = {
				.type = USB_BULK,
				.count = 8,
				.endpoint = 0x81,
				.u = {
					 .bulk = {
						 .buffersize = 0x10000,
					 }
				}
			},
			.size_of_priv = sizeof(struct mtvhd_adapter_state),
		},
	},

	.power_ctrl = mtvhd_v1_power_ctrl,

#ifdef CONFIG_DVB_USB_MTVHD_REMOTE_CONTROL
	.rc_interval      = 200,
	.rc_key_map       = mtvhd_rc_keys,
	.rc_key_map_size  = ARRAY_SIZE(mtvhd_rc_keys),
	.rc_query         = mtvhd_rc_query,
#endif

	.num_device_descs = 3,
	.devices = {
		{ .name = "SKNET MonsterTV HDUS ISDB-T USB2.0",
		  .cold_ids = { NULL },
		  .warm_ids = { &mtvhd_table[0], NULL },
		},
		{ .name = "Quixun QRS_NT100P ISDB-T PCI",
		  .cold_ids = { NULL },
		  .warm_ids = { &mtvhd_table[1], NULL },
		},
		{ .name = "SKNET MonsterTV HDP ISDB-T PCI",
		  .cold_ids = { NULL },
		  .warm_ids = { &mtvhd_table[3], NULL },
		},
	}
};

#ifdef CONFIG_DVB_USB_MTVHD_REMOTE_CONTROL
/* HDP : (ASIE5606 + xx90507) x 1 */
static struct dvb_usb_device_properties hdp_properties = {
	.caps = 0,
	.size_of_priv = sizeof(struct mtvhd_device_state),

	.num_adapters = 1,
	.adapter = {
		{
#if 0
			.caps = DVB_USB_ADAP_HAS_PID_FILTER |
				DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,

			.pid_filter_count = 8,
			.pid_filter       = mtvhd_pid_filter,
			.pid_filter_ctrl  = mtvhd_pid_filter_ctrl,
#endif
			.frontend_attach  = mtvhd_v1_frontend_attach,
			.streaming_ctrl   = mtvhd_stream_ctrl,
			.stream = {
				.type = USB_BULK,
				.count = 8,
				.endpoint = 0x81,
				.u = {
					 .bulk = {
						 .buffersize = 0x10000,
					 }
				}
			},
			.size_of_priv = sizeof(struct mtvhd_adapter_state),
		},
	},

	.power_ctrl = mtvhd_v1_power_ctrl,

	.num_device_descs = 2,
	.devices = {
		{ .name = "Quixun QRS_NT100P ISDB-T PCI",
		  .cold_ids = { NULL },
		  .warm_ids = { &mtvhd_table[1], NULL },
		},
		{ .name = "SKNET MonsterTV HDP ISDB-T PCI",
		  .cold_ids = { NULL },
		  .warm_ids = { &mtvhd_table[3], NULL },
		},
	}
};
#endif /* CONFIG_DVB_USB_MTVHD_REMOTE_CONTROL */
#endif /* CONFIG_DVB_USB_MTVHD_V1 */

static struct usb_driver mtvhd_driver = {
	.name		= "dvb_usb_mtvhd",
	.probe		= mtvhd_probe,
	.disconnect	= dvb_usb_device_exit,
	.id_table	= mtvhd_table,
};

/* module stuff */
static int __init mtvhd_module_init(void)
{
	int result;
	if ((result = usb_register(&mtvhd_driver))) {
		err("usb_register failed. Error number %d",result);
		return result;
	}

	return 0;
}

static void __exit mtvhd_module_exit(void)
{
	/* deregister this driver from the USB subsystem */
	usb_deregister(&mtvhd_driver);
}

module_init (mtvhd_module_init);
module_exit (mtvhd_module_exit);

MODULE_AUTHOR("Gombei Nanashi");
MODULE_DESCRIPTION("Driver for SKnet MonsterTV HD ISDB-T USB 2.0");
MODULE_VERSION("0.3");
MODULE_LICENSE("GPL");
