/* DVB USB framework compliant Linux driver for the SKNET MonsterTV HD ISDB-T
 * receiver. (Firmware downloader)
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
#include "asv5211.h"

/* debug */
static int dvb_usb_asv5211_debug;
module_param_named(debug, dvb_usb_asv5211_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info,2=xfer (or-able))." DVB_USB_DEBUG_STATUS);

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

#define deb_info(args...)	dprintk(dvb_usb_asv5211_debug, 0x01, args)
#define deb_xfer(args...)	dprintk(dvb_usb_asv5211_debug, 0x02, args)

/* Download firmware sub */
static int asv5211_send_firm(struct usb_device *udev,
	u8 req, u16 index, void *data, u16 value, u16 size)
{
	int ret;

	deb_xfer("USB control msg: 40 %02x %04x %04x %04x\n",
			req, value, index, size);
	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0), req,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			value, index, data, size, 5000);
	if (ret != size) {
		err("failed to download firmware at %04x (%04x)", value, ret);
		return -EIO;
	}

	return 0;
}

/* dvb-usb firmware_download entry */
#ifdef CONFIG_DVB_USB_ASV5211_WIN_DRIVER
static int asv5211_download_firmware(struct usb_device *udev,
		const struct firmware *fw)
{
	int ret = 0;
	u16 idx;
	u8 *buff, *fw_data;

	if (fw->size < 0xa90+0x4000) {
		err("firmware file is too small");
		return -EINVAL;
	}

	buff = kmalloc(0x1000, GFP_KERNEL);
	if (buff == NULL) {
		return -ENOMEM;
	}

	deb_info("download firmware: ");
	fw_data = (void *)fw->data;
	idx = le16_to_cpup((__le16 *)&fw_data[0xa88]);
	if ((idx == 0x4d66) /* CD 1.0 */ || (idx == 0x5121) /* CD 1.1 */) {
		deb_info("idx = 0x%x\n", idx);
	}
	else {
		err("unknown firmware file");
		ret = -EINVAL;
		goto done;
	}

	memcpy(buff, &fw_data[0xa90+0x0000], 0x0c00);
	ret = asv5211_send_firm(udev, 0xab, idx, buff, 0x0000, 0x0c00);
	if (ret != 0) {
		goto done;
	}
	memcpy(buff, &fw_data[0xa90+0x2000], 0x0400);
	ret = asv5211_send_firm(udev, 0xab, idx, buff, 0x2000, 0x0400);
	if (ret != 0) {
		goto done;
	}
	memcpy(buff, &fw_data[0xa90+0x2800], 0x1000);
	ret = asv5211_send_firm(udev, 0xab, idx, buff, 0x2800, 0x1000);
	if (ret != 0) {
		goto done;
	}
	memcpy(buff, &fw_data[0xa90+0x3800], 0x0800);
	ret = asv5211_send_firm(udev, 0xac, idx, buff, 0x3800, 0x0800);

done:
	kfree(buff);
	return ret;
}
#else	/* ! CONFIG_DVB_USB_ASV5211_WIN_DRIVER */
static int asv5211_download_firmware(struct usb_device *udev,
		const struct firmware *fw)
{
	int i;
	int ret = 0;
	u16 idx, ofs, len, sum;
	u8 *buff, *fw_data, req, sum_h;
	struct asv5211_fw_header *fw_header;
	struct asv5211_fw_block *fw_block;

	buff = kmalloc(0x1000, GFP_KERNEL);
	if (buff == NULL) {
		return -ENOMEM;
	}

	deb_info("download firmware: ");

	/* Locate header & check header contents */
	fw_header = (void *)fw->data;
	if (fw->size < sizeof(struct asv5211_fw_header)) {
		err("bad firmware file (size: %x < %x)", (unsigned int)fw->size, (unsigned int)sizeof(struct asv5211_fw_header));
		ret = -EINVAL;
		goto done;
	}
	if (strncmp("ASV5211", fw_header->name, 8) != 0) {
		err("bad firmware file (name)");
		ret = -EINVAL;
		goto done;
	}
	if (fw_header->header_size < sizeof(struct asv5211_fw_header) + sizeof(struct asv5211_fw_block) * fw_header->count) {
		err("bad firmware file (header size)");
		ret = -EINVAL;
		goto done;
	}
	if (fw->size < fw_header->header_size) {
		err("bad firmware file (size: %x < %x)", (unsigned int)fw->size, fw_header->header_size);
		ret = -EINVAL;
		goto done;
	}

	/* Locate block header */
	fw_data = (void *)fw->data;
	fw_block = (void *)(fw_data + sizeof(struct asv5211_fw_header));

	/* Check header checksum & seek to top of firmware data */
	sum_h = 0;
	for (i = 0; i < fw_header->header_size; i++) {
		sum_h += *fw_data;
		fw_data++;
	}
	if (sum_h != 0) {
		err("bad firmware file (header checksum: %02x)", sum_h);
		ret = -EINVAL;
		goto done;
	}

	/* Check checksum of firmware data */
	len = 0;
	for (i = 0; i < fw_header->count; i++) {
		len += le16_to_cpu(fw_block[i].length);
	}
	if (fw->size != fw_header->header_size + len) {
		err("bad firmware file (size: %x != %x + %x)", (unsigned int)fw->size, fw_header->header_size, len);
		ret = -EINVAL;
		goto done;
	}
	sum = 0;
	for (i = 0; i < len; i++) {
		sum += fw_data[i];
	}
	if (sum != le16_to_cpu(fw_header->sum)) {
		err("bad firmware file (checksum: %04x -> %04x)", le16_to_cpu(fw_header->sum), sum);
		ret = -EINVAL;
		goto done;
	}

	/* Download firmware */
	idx = le16_to_cpu(fw_header->version);
	deb_info("ASV5211 version? %04x\n", idx);
	for (i = 0; i < fw_header->count; i++) {
		ofs = le16_to_cpu(fw_block[i].offset);
		len = le16_to_cpu(fw_block[i].length);
		req = (i == fw_header->count - 1) ? 0xac : 0xab;
		if (len > 0x1000) {
			err("bad firmware file (FW[%d] length: %04x)", i, len);
			ret = -EINVAL;
			goto done;
		}
		memcpy(buff, fw_data, len);
		ret = asv5211_send_firm(udev, req, idx, buff, ofs, len);
		if (ret != 0) {
			goto done;
		}
		fw_data += len;
	}

done:
	kfree(buff);
	return ret;
}
#endif	/* ! CONFIG_DVB_USB_ASV5211_WIN_DRIVER */

/* USB Driver stuff */
static struct dvb_usb_device_properties asv5211_properties;

static int asv5211_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	return dvb_usb_device_init(intf, &asv5211_properties,
				THIS_MODULE, NULL, adapter_nr);
}

/* do not change the order of the ID table */
static struct usb_device_id asv5211_table [] = {
/* 00 */	{ USB_DEVICE(USB_VID_ASICEN,    USB_PID_ASICEN_ASV5211) },
		{ 0 }		/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, asv5211_table);

/* common cold state properties for as11loader  */
static struct dvb_usb_device_properties asv5211_properties = {
	.caps = 0,

	.usb_ctrl = DEVICE_SPECIFIC,
#ifdef CONFIG_DVB_USB_ASV5211_WIN_DRIVER
	.firmware = "AS11Loader.sys",
#else
	.firmware = "dvb-usb-asv5211.fw",
#endif
	.download_firmware = asv5211_download_firmware,
	.num_device_descs = 1,
	.devices = {
		{ .name = "ASICEN ASV5211 (cold state)",
		  .cold_ids = { &asv5211_table[0], NULL },
		  .warm_ids = { NULL },
		},
	}
};


static struct usb_driver asv5211_driver = {
	.name		= "dvb_usb_asv5211",
	.probe		= asv5211_probe,
	.disconnect	= dvb_usb_device_exit,
	.id_table	= asv5211_table,
};

/* module stuff */
static int __init asv5211_module_init(void)
{
	int result;
	if ((result = usb_register(&asv5211_driver))) {
		err("usb_register failed. Error number %d", result);
		return result;
	}

	return 0;
}

static void __exit asv5211_module_exit(void)
{
	/* deregister this driver from the USB subsystem */
	usb_deregister(&asv5211_driver);
}

module_init (asv5211_module_init);
module_exit (asv5211_module_exit);

MODULE_AUTHOR("Gombei Nanashi");
MODULE_DESCRIPTION("Firmware downloader for ASICEN ASV5211");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");
