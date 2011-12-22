/* DVB USB framework compliant Linux driver for the SKNET MonsterTV HD ISDB-T
 * receiver. (Local decryption: DES)
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
#include "des.h"

struct mtvhd_des_ctx {
	gl_des_ctx ctx[2];
	uint8_t xorkey[8];
};

/* Fixed DES subkey for ASIE5606 */
static uint32_t mtvhd_des_subkey[][32] = {
	{
		0x07360B38, 0x102C0B0D, 0x2C242439, 0x1D183310,
		0x2916350C, 0x3204122D, 0x083F0D38, 0x0B092406,
		0x352A1125, 0x29003038, 0x20302238, 0x2A2F362A,
		0x0719151D, 0x27280C2C, 0x33183332, 0x011D0402,
		0x261D0617, 0x0C06260F, 0x10313C01, 0x07140D0A,
		0x303D2203, 0x31223415, 0x3A042A0C, 0x270A2B0C,
		0x212A0702, 0x063A1C1F, 0x032C082B, 0x3E011F00,
		0x0C0C270A, 0x20393131, 0x02121903, 0x3D131236
	},
	{
		0x39102830, 0x03220000, 0x1409041A, 0x30042401,
		0x18151900, 0x002E0108, 0x13030220, 0x05222411,
		0x32200804, 0x0C130A28, 0x04040612, 0x36120412,
		0x081C1001, 0x140C0B00, 0x09272602, 0x01002001,
		0x0A001003, 0x361C1424, 0x091B2018, 0x10180100,
		0x19061304, 0x19011005, 0x2C020828, 0x08230802,
		0x06320107, 0x0E041030, 0x00192038, 0x29140302,
		0x30151501, 0x11080025, 0x04290000, 0x0E151B28
	}
};

static uint8_t mtvhd_des_xorkey[] = {
	0x00, 0x00, 0xB1, 0xF2, 0x00, 0x04, 0xF2, 0x04
};


/* Allocate crypto context & set keys */
int mtvhd_des_init(struct mtvhd_adapter_state *st)
{
	struct mtvhd_des_ctx *ctx;
	int i, j;

	if (st == NULL) {
		return -EUNATCH;
	}
	ctx = kzalloc(sizeof(struct mtvhd_des_ctx), GFP_KERNEL);
	if (ctx == NULL) {
		return -ENOMEM;
	}
	st->crypto_ctx = ctx;

	/* Initialize context with fixed keys */
	for (i = 0; i < 2; i++) {
		memcpy(ctx->ctx[i].encrypt_subkeys, mtvhd_des_subkey[i], sizeof(uint32_t) * 32);
		for (j = 0; j < 32; j += 2) {
			ctx->ctx[i].decrypt_subkeys[j] = ctx->ctx[i].encrypt_subkeys[30 - j];
			ctx->ctx[i].decrypt_subkeys[j + 1] = ctx->ctx[i].encrypt_subkeys[31 - j];
		}
	}
	memcpy(ctx->xorkey, mtvhd_des_xorkey, sizeof(mtvhd_des_xorkey));

	return 0;
}

/* Release crypto context */
void mtvhd_des_release(struct mtvhd_adapter_state *st)
{
	if (st == NULL || st->crypto_ctx == NULL) {
		return;
	}

	kfree(st->crypto_ctx);
	st->crypto_ctx = NULL;

	return;
}

/* Decrypt TS packet with DES (gnulib DES library) */
void mtvhd_des_decrypt(void *context, uint8_t *data)
{
	struct mtvhd_des_ctx *ctx = context;
	int i, j;

	/* Skip first 4 byte header */
	/* Decrypt 128byte with key0 */
	for (i = 4; i < 4 + 128; i += 8) {
		for (j = 0; j < 8; j++) {
			data[i + j] ^= ctx->xorkey[j];
		}
		gl_des_ecb_decrypt(&ctx->ctx[0], &data[i], &data[i]);
	}
	/* Decrypt remaining bytes with key1 */
	for (; i < TS_SIZE; i += 8) {
		for (j = 0; j < 8; j++) {
			data[i + j] ^= ctx->xorkey[j];
		}
		gl_des_ecb_decrypt(&ctx->ctx[1], &data[i], &data[i]);
	}
}
