/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2008-2011  Intel Corporation. All rights reserved.
 *  Copyright (C) 2013 Jolla Ltd
 *  Copyright (C) 2014 Canonical Ltd
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

#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <glib.h>

#include <ofono/log.h>
#include <ofono/modem.h>
#include <ofono/cbs.h>

#include "gril.h"
#include "grilutil.h"
#include "grilrequest.h"
#include "grilunsol.h"

#include "rilmodem.h"
#include "ril_constants.h"

struct cbs_data {
	GRil *ril;
};

static void ril_set_topics_cb(struct ril_msg *message, gpointer user_data)
{
	struct cb_data *cbd = user_data;
	struct ofono_cbs *cbs = cbd->user;
	struct cbs_data *cd = ofono_cbs_get_data(cbs);
	ofono_cbs_set_cb_t cb = cbd->cb;

	if (message->error == RIL_E_SUCCESS) {
		g_ril_print_response_no_args(cd->ril, message);
		CALLBACK_WITH_SUCCESS(cb, cbd->data);
	} else {
		ofono_error("%s: set topics error %s", __func__,
				ril_error_to_string(message->error));
		CALLBACK_WITH_FAILURE(cb, cbd->data);
	}
}

static void ril_set_topics(struct ofono_cbs *cbs, const char *topics,
				ofono_cbs_set_cb_t cb, void *user_data)
{
	struct parcel rilp;
	struct cbs_data *cd = ofono_cbs_get_data(cbs);
	struct cb_data *cbd = cb_data_new(cb, user_data, cbs);

	DBG("%s", topics);

	g_ril_request_gsm_set_broadcast_sms_config(cd->ril, topics, &rilp);

	if (g_ril_send(cd->ril, RIL_REQUEST_GSM_SET_BROADCAST_SMS_CONFIG, &rilp,
			ril_set_topics_cb, cbd, g_free) == 0) {
		ofono_error("%s: cannot send request", __func__);
		g_free(cbd);
		CALLBACK_WITH_FAILURE(cb, user_data);
	}
}

static void ril_clear_topics_cb(struct ril_msg *message, gpointer user_data)
{
	struct cb_data *cbd = user_data;
	struct ofono_cbs *cbs = cbd->user;
	struct cbs_data *cd = ofono_cbs_get_data(cbs);
	ofono_cbs_set_cb_t cb = cbd->cb;

	if (message->error == RIL_E_SUCCESS) {
		g_ril_print_response_no_args(cd->ril, message);
		CALLBACK_WITH_SUCCESS(cb, cbd->data);
	} else {
		ofono_error("%s: clear topics error %s", __func__,
				ril_error_to_string(message->error));
		CALLBACK_WITH_FAILURE(cb, cbd->data);
	}
}

static void ril_clear_topics(struct ofono_cbs *cbs,
				ofono_cbs_set_cb_t cb, void *user_data)
{
	struct parcel rilp;
	struct cbs_data *cd = ofono_cbs_get_data(cbs);
	struct cb_data *cbd = cb_data_new(cb, user_data, cbs);

	g_ril_request_gsm_sms_broadcast_activation(cd->ril, 0, &rilp);

	if (g_ril_send(cd->ril, RIL_REQUEST_GSM_SMS_BROADCAST_ACTIVATION, &rilp,
			ril_clear_topics_cb, cbd, g_free) == 0) {
		ofono_error("%s: cannot activate CBS", __func__);
		g_free(cbd);
		CALLBACK_WITH_FAILURE(cb, user_data);
	}
}

static void ril_cbs_notify(struct ril_msg *message, gpointer user_data)
{
	struct ofono_cbs *cbs = user_data;
	struct cbs_data *cd = ofono_cbs_get_data(cbs);
	unsigned char *cbs_pdu;

	ofono_info("%s BROADCAST SMS", __func__);

	cbs_pdu = g_ril_unsol_parse_broadcast_sms(cd->ril, message);
	if (cbs_pdu == NULL) {
		ofono_error("%s: Parsing error", __func__);
		return;
	}

	/*
	 * ofono does not support UMTS CB - see src/smsutil.c method cbs_decode.
	 * But let's let the core make the rejection.
	 */

	ofono_cbs_notify(cbs, cbs_pdu, strlen((char *) cbs_pdu));

	g_free(cbs_pdu);
}

static gboolean ril_delayed_register(gpointer user_data)
{
	struct ofono_cbs *cbs = user_data;
	struct cbs_data *cd = ofono_cbs_get_data(cbs);

	ofono_cbs_register(cbs);

	g_ril_register(cd->ril, RIL_UNSOL_RESPONSE_NEW_BROADCAST_SMS,
			ril_cbs_notify,	cbs);

	/* This makes the timeout a single-shot */
	return FALSE;
}

static int ril_cbs_probe(struct ofono_cbs *cbs, unsigned int vendor,
				void *user)
{
	GRil *ril = user;
	struct cbs_data *cd;

	cd = g_try_new0(struct cbs_data, 1);
	if (cd == NULL)
		return -ENOMEM;

	cd->ril = g_ril_clone(ril);

	ofono_cbs_set_data(cbs, cd);

	g_idle_add(ril_delayed_register, cbs);

	return 0;
}

static void ril_cbs_remove(struct ofono_cbs *cbs)
{
	struct cbs_data *cd = ofono_cbs_get_data(cbs);

	ofono_cbs_set_data(cbs, NULL);

	g_ril_unref(cd->ril);
	g_free(cd);
}

static struct ofono_cbs_driver driver = {
	.name		= RILMODEM,
	.probe		= ril_cbs_probe,
	.remove		= ril_cbs_remove,
	.set_topics	= ril_set_topics,
	.clear_topics	= ril_clear_topics
};

void ril_cbs_init(void)
{
	ofono_cbs_driver_register(&driver);
}

void ril_cbs_exit(void)
{
	ofono_cbs_driver_unregister(&driver);
}
