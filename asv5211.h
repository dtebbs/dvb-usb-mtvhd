/* Common header-file of the Linux driver for the SKNET MonsterTV HD ISDB-T
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
#ifndef _DVB_USB_ASV5211_H_
#define _DVB_USB_ASV5211_H_

#define DVB_USB_LOG_PREFIX "asv5211"
#include "dvb-usb.h"

#ifndef CONFIG_DVB_USB_ASV5211_WIN_DRIVER
struct asv5211_fw_block
{
	__le16	offset;
	__le16	length;
};

struct asv5211_fw_header
{
	char		name[8];	/* 00: "ASV5211" */
	__le16		version;	/* 08: Firmware version ?? */
	__le16		sum;		/* 0A: Checksum */
	uint8_t		header_size;	/* 0C: Header size */
	uint8_t		header_sum;	/* 0D: Checksum for header */
	uint8_t		reserved;	/* 0E */
	uint8_t		count;		/* 0F: Number of FW block */
};
#endif	/* ! CONFIG_DVB_USB_ASV5211_WIN_DRIVER */

#endif	/* _DVB_USB_ASV5211_H_ */
