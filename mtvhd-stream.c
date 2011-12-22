/* DVB USB framework compliant Linux driver for the SKNET MonsterTV HD ISDB-T
 * receiver. (Post process of stream completion: local decryption)
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

/* Decrypt stream */
static void mtvhd_stream_complete(struct usb_data_stream *stream,
					u8 *data, size_t len)
{
	struct dvb_usb_adapter *adap = stream->user_priv;
	struct mtvhd_adapter_state *st = adap->priv;
	u8 *p, *p1;

	p = data;
	if (st->buff_used) {
		if (len < TS_SIZE - st->buff_used) {
			memcpy(&st->packet_buff[st->buff_used], data, len);
			st->buff_used += len;
			return;
		}
		memcpy(&st->packet_buff[st->buff_used], data, TS_SIZE - st->buff_used);
		st->decrypt(st->crypto_ctx, st->packet_buff);
		st->complete_orig(stream, st->packet_buff, TS_SIZE);
		p += TS_SIZE - st->buff_used;
		st->buff_used = 0;
	}

	p1 = p;
	while (p < data + len) {
		if (*p != 0x47) {
			p++;
			continue;
		}
		if (p + TS_SIZE > data + len) {
			break;
		}
		st->decrypt(st->crypto_ctx, p);
		p += TS_SIZE;
	}
	if (p != p1) {
		st->complete_orig(stream, p1, p - p1);
	}

	/* save the remaining data */
	if (p < data + len) {
		st->buff_used = data + len - p;
		memcpy(st->packet_buff, p, st->buff_used);
	}

	return;
}

/* Dummy routine for passthrough */
static void mtvhd_dummy_decrypt(void *ctx, u8 *data)
{
	return;
}

/* Release context for current crypto library */
void mtvhd_crypto_release(struct mtvhd_adapter_state *st)
{
	switch (st->crypto_mode) {
	  case MTVHD_CRYPTO_DES:
		mtvhd_des_release(st);
		break;

	  default:
		; /* Nothing to do */
	}

	st->decrypt = mtvhd_dummy_decrypt;
	st->crypto_mode = 0;

	return;
}

/* Initialize for specific crypto library */
int mtvhd_crypto_init(struct mtvhd_adapter_state *st, int mode)
{
	int ret = 0;

	if (mode == st->crypto_mode) {
		return 0;
	}
	if (st->crypto_mode != 0) {
		mtvhd_crypto_release(st);
	}

	switch (mode) {
#ifdef CONFIG_DVB_USB_MTVHD_V1
	  case MTVHD_CRYPTO_XOR:
		st->decrypt = mtvhd_xor_decrypt;
		st->crypto_mode = MTVHD_CRYPTO_XOR;
		deb_info("Crypto algorithm: XOR\n");
		break;
#endif

	  case MTVHD_CRYPTO_DES:
		ret = mtvhd_des_init(st);
		if (ret == 0) {
			st->decrypt = mtvhd_des_decrypt;
			st->crypto_mode = MTVHD_CRYPTO_DES;
			deb_info("Crypto algorithm: DES\n");
		}
		break;

	  case 0:
		/* Just release */
		break;

	  default:
		ret = -EINVAL;
	}

	return ret;
}

/* Hook streaming complete process */
int mtvhd_stream_init(struct dvb_usb_adapter *adap)
{
	struct mtvhd_adapter_state *st = adap->priv;

	st->decrypt = mtvhd_dummy_decrypt;
	st->complete_orig = adap->stream.complete;
	adap->stream.complete = mtvhd_stream_complete;

	return 0;
}
