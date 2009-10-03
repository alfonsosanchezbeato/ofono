/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2008-2009  Intel Corporation. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdlib.h>

#include <glib.h>

#include <ofono/types.h>
#include "simutil.h"
#include "util.h"
#include "smsutil.h"

struct sim_eons {
	struct sim_eons_operator_info *pnn_list;
	GSList *opl_list;
	gboolean pnn_valid;
	int pnn_max;
};

struct spdi_operator {
	char mcc[OFONO_MAX_MCC_LENGTH + 1];
	char mnc[OFONO_MAX_MNC_LENGTH + 1];
};

struct opl_operator {
	char mcc[OFONO_MAX_MCC_LENGTH + 1];
	char mnc[OFONO_MAX_MNC_LENGTH + 1];
	guint16 lac_tac_low;
	guint16 lac_tac_high;
	guint8 id;
};

#define BINARY 0
#define RECORD 1
#define CYCLIC 3

#define ALW	0
#define PIN	1
#define PIN2	2
#define ADM	4
#define NEV	15

static struct sim_ef_info ef_db[] = {
{	0x2F05, ROOTMF, BINARY, 0,	ALW,	PIN	},
{	0x2F06, ROOTMF, RECORD, 0,	ALW,	PIN	},
{	0x2FE2, ROOTMF, BINARY, 10,	ALW,	NEV 	},
{	0x6F05, 0x7F20, BINARY, 0,	ALW,	PIN	},
{	0x6F06, 0x0000, RECORD, 0,	ALW,	ADM	},
{	0x6F2C, 0x7F20, BINARY, 16,	PIN,	PIN	},
{	0x6F30, 0x7F20, BINARY, 0,	PIN,	PIN	},
{	0x6F32, 0x7F20, BINARY, 0,	PIN,	ADM	},
{	0x6F37, 0x7F20, BINARY, 3,	PIN,	PIN2	},
{	0x6F38, 0x7F20, BINARY, 0,	PIN,	ADM	},
{	0x6F39, 0x7F20, CYCLIC, 3,	PIN,	PIN2	},
{	0x6F3B, 0x7F10, RECORD, 0,	PIN,	PIN2	},
{	0x6F3E, 0x7F20, BINARY, 0,	PIN,	ADM	},
{	0x6F3F, 0x7F20, BINARY, 0,	PIN,	ADM	},
{	0x6F40, 0x7F10, RECORD, 0,	PIN,	PIN	},
{	0x6F41, 0x7F20, BINARY, 5,	PIN,	PIN2	},
{	0x6F42, 0x7F10, RECORD, 0,	PIN,	PIN	},
{	0x6F44, 0x7F10, CYCLIC, 0,	PIN,	PIN	},
{	0x6F45, 0x7F20, BINARY, 0,	PIN,	PIN	},
{	0x6F46, 0x7F20, BINARY, 17,	ALW,	ADM	},
{	0x6F48, 0x7F20, BINARY, 0,	PIN,	ADM	},
{	0x6F49, 0x7F10, RECORD, 0,	PIN,	ADM	},
{	0x6F4D, 0x7F20, RECORD, 0,	PIN,	PIN2	},
{	0x6F50, 0x7F20, BINARY, 0,	PIN,	PIN	},
{	0x6F51, 0x7F20, RECORD, 0,	PIN,	ADM	},
{	0x6F53, 0x7F20, BINARY, 14,	PIN,	PIN	},
{	0x6F56, 0x0000, BINARY, 0,	PIN,	PIN2	},
{	0x6F60, 0x7F20, BINARY, 0,	PIN,	PIN	},
{	0x6F61, 0x7F20, BINARY, 0,	PIN,	ADM	},
{	0x6F62, 0x7F20, BINARY, 0,	PIN,	ADM	},
{	0x6F73, 0x0000, BINARY, 14,	PIN,	PIN	},
{	0x6F7B, 0x7F20, BINARY, 0,	PIN,	PIN	},
{	0x6F7E, 0x7F20, BINARY, 11,	PIN,	PIN	},
{	0x6FAD, 0x7F20, BINARY, 0,	ALW,	ADM	},
{	0x6FAE, 0x7F20, BINARY, 1,	ALW,	ADM	},
{	0x6FB7, 0x7F20, BINARY, 0,	ALW,	ADM	},
{	0x6FC5, 0x7F20, RECORD, 0,	ALW,	ADM	},
{	0x6FC6, 0x7F20, RECORD, 0,	ALW,	ADM	},
{	0x6FC7, 0x7F20, RECORD, 0,	PIN,	PIN	},
{	0x6FC9, 0x7F20, RECORD, 0,	PIN,	PIN	},
{	0x6FCA, 0x7F20, RECORD, 0,	PIN,	PIN	},
{	0x6FCB, 0x7F20, RECORD, 16,	PIN,	PIN	},
{	0x6FCD, 0x7F20, BINARY, 0,	PIN,	ADM	},
{	0x6FD9, 0x0000, BINARY, 0,	PIN,	ADM	},
{	0x6FDB, 0x0000, BINARY, 1,	PIN,	ADM	},
{	0x6FDC, 0x0000, BINARY, 1,	PIN,	ADM	},
{	0x6FDE, 0x0000, BINARY, 0,	ALW,	ADM	},
{	0x6FDF, 0x0000, RECORD, 0,	ALW,	ADM	},
{	0x6FE3, 0x0000, BINARY, 18,	PIN,	PIN	},
};

/* Parse ASN.1 Basic Encoding Rules TLVs per ISO/IEC 7816 */
static const guint8 *ber_tlv_find_by_tag(const guint8 *pdu, guint8 in_tag,
						int in_len, int *out_len)
{
	guint8 tag;
	int len;
	const guint8 *end = pdu + in_len;

	do {
		while (pdu < end && (*pdu == 0x00 || *pdu == 0xff))
			pdu++;
		if (pdu == end)
			break;

		tag = *pdu++;
		if (!(0x1f & ~tag))
			while (pdu < end && (*pdu++ & 0x80))
				;
		if (pdu == end)
			break;

		for (len = 0; pdu + 1 < end && (*pdu & 0x80);
				len = (len | (*pdu++ & 0x7f)) << 7)
			;

		if (*pdu & 0x80)
			break;
		len |= *pdu++;

		if (tag == in_tag && pdu + len <= end) {
			if (out_len)
				*out_len = len;
			return pdu;
		}

		pdu += len;
	} while (pdu < end);

	return NULL;
}

static char *sim_network_name_parse(const unsigned char *buffer, int length,
					gboolean *add_ci)
{
	char *ret = NULL;
	unsigned char *endp;
	unsigned char dcs;
	int i;
	gboolean ci = FALSE;

	if (length < 1)
		return NULL;

	dcs = *buffer++;
	length--;

	/* "The MS should add the letters for the Country's Initials and a
	 * separator (e.g. a space)" */
	if (is_bit_set(dcs, 4))
		ci = TRUE;

	switch (dcs & (7 << 4)) {
	case 0x00:
		endp = memchr(buffer, 0xff, length);
		if (endp)
			length = endp - buffer;
		ret = convert_gsm_to_utf8(buffer, length,
				NULL, NULL, 0xff);
		break;
	case 0x10:
		if ((length % 2) == 1) {
			if (buffer[length - 1] != 0xff)
				return NULL;

			length = length - 1;
		}

		for (i = 0; i < length; i += 2)
			if (buffer[i] == 0xff && buffer[i + 1] == 0xff)
				break;

		ret = g_convert((const char *)buffer, length,
					"UTF-8//TRANSLIT", "UCS-2BE",
					NULL, NULL, NULL);
		break;
	}

	if (add_ci)
		*add_ci = ci;

	return ret;
}

static void parse_mcc_mnc(const guint8 *bcd, char *mcc, char *mnc)
{
	static const char digit_lut[] = "0123456789*#abd\0";
	guint8 digit;

	digit = (bcd[0] >> 0) & 0xf;
	*mcc++ = digit_lut[digit];

	digit = (bcd[0] >> 4) & 0xf;
	*mcc++ = digit_lut[digit];

	digit = (bcd[1] >> 0) & 0xf;
	*mcc++ = digit_lut[digit];

	digit = (bcd[2] >> 0) & 0xf;
	*mnc++ = digit_lut[digit];

	digit = (bcd[2] >> 4) & 0xf;
	*mnc++ = digit_lut[digit];

	digit = (bcd[1] >> 4) & 0xf;
	*mnc++ = digit_lut[digit];
}

static gint spdi_operator_compare(gconstpointer a, gconstpointer b)
{
	const struct spdi_operator *opa = a;
	const struct spdi_operator *opb = b;
	gint r = strcmp(opa->mcc, opb->mcc);

	if (r)
		return r;

	return strcmp(opa->mnc, opb->mnc);
}

struct sim_spdi {
	GSList *operators;
};

struct sim_spdi *sim_spdi_new(const guint8 *tlv, int length)
{
	const guint8 *plmn_list;
	struct sim_spdi *spdi;
	struct spdi_operator *oper;
	int tlv_length;

	if (length <= 5)
		return NULL;

	plmn_list = ber_tlv_find_by_tag(tlv, 0x80, length, &tlv_length);

	if (!plmn_list)
		return NULL;

	spdi = g_new0(struct sim_spdi, 1);

	for (tlv_length /= 3; tlv_length--; plmn_list += 3) {
		if ((plmn_list[0] & plmn_list[1] & plmn_list[2]) == 0xff)
			continue;

		oper = g_new0(struct spdi_operator, 1);

		parse_mcc_mnc(plmn_list, oper->mcc, oper->mnc);
		spdi->operators = g_slist_insert_sorted(spdi->operators, oper,
						spdi_operator_compare);
	}

	return spdi;
}

gboolean sim_spdi_lookup(struct sim_spdi *spdi,
				const char *mcc, const char *mnc)
{
	struct spdi_operator spdi_op;

	if (!spdi)
		return FALSE;

	g_strlcpy(spdi_op.mcc, mcc, sizeof(spdi_op.mcc));
	g_strlcpy(spdi_op.mnc, mnc, sizeof(spdi_op.mnc));

	return g_slist_find_custom(spdi->operators, &spdi_op,
					spdi_operator_compare) != NULL;
}

void sim_spdi_free(struct sim_spdi *spdi)
{
	g_slist_foreach(spdi->operators, (GFunc)g_free, NULL);
	g_slist_free(spdi->operators);
	g_free(spdi);
}

static void pnn_operator_free(struct sim_eons_operator_info *oper)
{
	g_free(oper->info);
	g_free(oper->shortname);
	g_free(oper->longname);
}

struct sim_eons *sim_eons_new(int pnn_records)
{
	struct sim_eons *eons = g_new0(struct sim_eons, 1);

	eons->pnn_list = g_new0(struct sim_eons_operator_info, pnn_records);
	eons->pnn_max = pnn_records;

	return eons;
}

gboolean sim_eons_pnn_is_empty(struct sim_eons *eons)
{
	return !eons->pnn_valid;
}

void sim_eons_add_pnn_record(struct sim_eons *eons, int record,
				const guint8 *tlv, int length)
{
	const unsigned char *name;
	int namelength;
	struct sim_eons_operator_info *oper = &eons->pnn_list[record-1];

	name = ber_tlv_find_by_tag(tlv, 0x43, length, &namelength);

	if (!name || !namelength)
		return;

	oper->longname = sim_network_name_parse(name, namelength,
						&oper->long_ci);

	name = ber_tlv_find_by_tag(tlv, 0x45, length, &namelength);

	if (name && namelength)
		oper->shortname = sim_network_name_parse(name, namelength,
							&oper->short_ci);

	name = ber_tlv_find_by_tag(tlv, 0x80, length, &namelength);

	if (name && namelength)
		oper->info = sim_string_to_utf8(name, namelength);

	eons->pnn_valid = TRUE;
}

static struct opl_operator *opl_operator_alloc(const guint8 *record)
{
	struct opl_operator *oper = g_new0(struct opl_operator, 1);

	parse_mcc_mnc(record, oper->mcc, oper->mnc);
	record += 3;

	oper->lac_tac_low = (record[0] << 8) | record[1];
	record += 2;
	oper->lac_tac_high = (record[0] << 8) | record[1];
	record += 2;

	oper->id = record[0];

	return oper;
}

void sim_eons_add_opl_record(struct sim_eons *eons,
				const guint8 *contents, int length)
{
	struct opl_operator *oper;

	oper = opl_operator_alloc(contents);

	if (oper->id > eons->pnn_max) {
		g_free(oper);
		return;
	}

	eons->opl_list = g_slist_prepend(eons->opl_list, oper);
}

void sim_eons_optimize(struct sim_eons *eons)
{
	eons->opl_list = g_slist_reverse(eons->opl_list);
}

void sim_eons_free(struct sim_eons *eons)
{
	int i;

	for (i = 0; i < eons->pnn_max; i++)
		pnn_operator_free(eons->pnn_list + i);

	g_free(eons->pnn_list);

	g_slist_foreach(eons->opl_list, (GFunc)g_free, NULL);
	g_slist_free(eons->opl_list);

	g_free(eons);
}

static const struct sim_eons_operator_info *
	sim_eons_lookup_common(struct sim_eons *eons,
				const char *mcc, const char *mnc,
				gboolean have_lac, guint16 lac)
{
	GSList *l;
	const struct opl_operator *opl;
	int i;

	for (l = eons->opl_list; l; l = l->next) {
		opl = l->data;

		for (i = 0; i < OFONO_MAX_MCC_LENGTH; i++)
			if (mcc[i] != opl->mcc[i] &&
					!(opl->mcc[i] == 'b' && mcc[i]))
				break;
		if (i < OFONO_MAX_MCC_LENGTH)
			continue;

		for (i = 0; i < OFONO_MAX_MNC_LENGTH; i++)
			if (mnc[i] != opl->mnc[i] &&
					!(opl->mnc[i] == 'b' && mnc[i]))
				break;
		if (i < OFONO_MAX_MNC_LENGTH)
			continue;

		if (opl->lac_tac_low == 0 && opl->lac_tac_high == 0xfffe)
			break;

		if (have_lac == FALSE)
			continue;

		if ((lac >= opl->lac_tac_low) && (lac <= opl->lac_tac_high))
			break;
	}

	if (!l)
		return NULL;

	opl = l->data;

	/* 0 is not a valid record id */
	if (opl->id == 0)
		return NULL;

	return &eons->pnn_list[opl->id - 1];
}

const struct sim_eons_operator_info *sim_eons_lookup(struct sim_eons *eons,
						const char *mcc,
						const char *mnc)
{
	return sim_eons_lookup_common(eons, mcc, mnc, FALSE, 0);
}

const struct sim_eons_operator_info *sim_eons_lookup_with_lac(
							struct sim_eons *eons,
							const char *mcc,
							const char *mnc,
							guint16 lac)
{
	return sim_eons_lookup_common(eons, mcc, mnc, TRUE, lac);
}

gboolean sim_adn_parse(const unsigned char *data, int length,
			struct ofono_phone_number *ph, char **identifier)
{
	int number_len;
	int ton_npi;
	const unsigned char *alpha;
	int alpha_length;

	if (length < 14)
		return FALSE;

	alpha = data;
	alpha_length = length - 14;

	data += alpha_length;

	number_len = *data++;
	ton_npi = *data++;

	if (number_len > 11 || ton_npi == 0xff)
		return FALSE;

	ph->type = ton_npi;

	/* BCD coded, however the TON/NPI is given by the first byte */
	number_len -= 1;
	extract_bcd_number(data, number_len, ph->number);

	if (identifier == NULL)
		return TRUE;

	/* Alpha-Identifier field */
	if (alpha_length > 0)
		*identifier = sim_string_to_utf8(alpha, alpha_length);
	else
		*identifier = NULL;

	return TRUE;
}

void sim_adn_build(unsigned char *data, int length,
			const struct ofono_phone_number *ph,
			const char *identifier)
{
	int number_len = strlen(ph->number);
	unsigned char *gsm_identifier = NULL;
	long gsm_bytes;
	long alpha_length;

	alpha_length = length - 14;

	/* Alpha-Identifier field */
	if (alpha_length > 0) {
		memset(data, 0xff, alpha_length);

		if (identifier)
			gsm_identifier = convert_utf8_to_gsm(identifier,
					-1, NULL, &gsm_bytes, 0);

		if (gsm_identifier) {
			memcpy(data, gsm_identifier,
				MIN(gsm_bytes, alpha_length));
			g_free(gsm_identifier);
		}

		/* TODO: figure out when the identifier needs to
		 * be encoded in UCS2 and do this.
		 */
		data += alpha_length;
	}

	number_len = (number_len + 1) / 2;
	*data++ = number_len + 1;

	/* Use given number type and 'Unknown' for Numbering Plan */
	*data++ = ph->type;

	encode_bcd_number(ph->number, data);
	memset(data + number_len, 0xff, 10 - number_len);
	data += 10;

	/* CCP1 unused */
	*data++ = 0xff;
	/* Ext1 unused */
	*data++ = 0xff;
}

static int find_ef_by_id(const void *key, const void *value)
{
	unsigned short id = GPOINTER_TO_UINT(key);
	const struct sim_ef_info *info = value;

	return id - info->id;
}

struct sim_ef_info *sim_ef_db_lookup(unsigned short id)
{
	struct sim_ef_info *result;
	unsigned int nelem = sizeof(ef_db) / sizeof(struct sim_ef_info);

	result = bsearch(GUINT_TO_POINTER(id), ef_db, nelem,
				sizeof(struct sim_ef_info), find_ef_by_id);

	return result;
}

gboolean sim_parse_3g_get_response(const unsigned char *data, int len,
					int *file_len, int *record_len,
					int *structure, unsigned char *access,
					unsigned short *efid)
{
	const unsigned char *fcp;
	int fcp_length;
	const unsigned char *tlv;
	int tlv_length;
	int i;
	int flen, rlen, str;
	unsigned short id;
	unsigned char acc[3];
	struct sim_ef_info *info;

	fcp = ber_tlv_find_by_tag(data, 0x62, len, &fcp_length);

	if (fcp == NULL)
		return FALSE;

	/* Find the file size tag 0x80 according to
	 * ETSI 102.221 Section 11.1.1.3.2 */
	tlv = ber_tlv_find_by_tag(fcp, 0x80, fcp_length, &tlv_length);

	if (!tlv || tlv_length < 2)
		return FALSE;

	flen = tlv[0];
	for (i = 1; i < tlv_length; i++)
		flen = (flen << 8) | tlv[i];

	tlv = ber_tlv_find_by_tag(fcp, 0x83, fcp_length, &tlv_length);

	if (!tlv || tlv_length != 2)
		return FALSE;

	id = (tlv[0] << 8) | tlv[1];

	tlv = ber_tlv_find_by_tag(fcp, 0x82, fcp_length, &tlv_length);

	if (!tlv || (tlv_length != 2 && tlv_length != 5))
		return FALSE;

	if (tlv[1] != 0x21)
		return FALSE;

	switch (tlv[0] & 0x3) {
	case 1:	/* Transparent */
		str = 0x00;
		break;
	case 2: /* Linear Fixed */
		str = 0x01;
		break;
	case 6: /* Cyclic */
		str = 0x03;
		break;
	default:
		return FALSE;
	};

	/* For cyclic or linear fixed we need record size & num records */
	if (str != 0x00 && tlv_length != 5)
		return FALSE;

	/* strictly speaking the record length is 16 bit, but the valid
	 * range is 0x01 to 0xFF according to 102.221 */
	if (str != 0x00)
		rlen = tlv[3];
	else
		rlen = 0;

	/* The 3G response data contains references to EFarr which actually
	 * contains the security attributes.  These are usually not carried
	 * along with the response data unlike in 2G.  Instead of querying
	 * this, we simply look it up in our database.  We fudge it somewhat
	 * and guess if the file isn't found.
	 */
	info = sim_ef_db_lookup(id);

	if (str == 0x03)
		acc[1] = 0x1f;
	else
		acc[1] = 0xff;

	acc[2] = 0x44;

	if (!info)
		acc[0] = 0x11;
	else
		acc[0] = (info->perm_read << 4) | info->perm_update;

	if (file_len)
		*file_len = flen;

	if (record_len)
		*record_len = rlen;

	if (efid)
		*efid = id;

	if (structure)
		*structure = str;

	if (access)
		memcpy(access, acc, 3);

	return TRUE;
}

gboolean sim_parse_2g_get_response(const unsigned char *response, int len,
					int *file_len, int *record_len,
					int *structure, unsigned char *access)
{
	if (len < 14 || response[6] != 0x04)
		return FALSE;

	if ((response[13] == 0x01 || response[13] == 0x03) && len < 15)
		return FALSE;

	*file_len = (response[2] << 8) | response[3];
	*structure = response[13];

	access[0] = response[8];
	access[1] = response[9];
	access[2] = response[10];

	if (response[13] == 0x01 || response[13] == 0x03)
		*record_len = response[14];
	else
		*record_len = 0;

	return TRUE;
}
