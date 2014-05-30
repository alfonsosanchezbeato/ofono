/*
 *
 *  oFono - Open Source Telephony - RIL Modem Support
 *
 *  Copyright (C) 2014 Canonical Ltd.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <glib.h>

#include <ofono/log.h>
#include <ofono/modem.h>
#include <ofono/cell-info.h>

#include "gril.h"

#include "rilmodem.h"
#include "grilreply.h"
#include "grilunsol.h"

static void query_cb(struct ril_msg *message, gpointer user_data)
{
	struct cb_data *cbd = user_data;
	ofono_cell_info_query_list_cb_t cb = cbd->cb;
	GRil *ril = cbd->user;
	struct ofono_error error;
	GSList *results;

	if (message->error == RIL_E_SUCCESS) {
		decode_ril_error(&error, "OK");
	} else {
		decode_ril_error(&error, "FAIL");
		cb(&error, NULL, cbd->data);
		return;
	}

	results = g_ril_unsol_parse_cell_info_list(ril, message);

	cb(&error, results, cbd->data);
}

static void ril_cell_info_query_list(struct ofono_cell_info *ci,
				ofono_cell_info_query_list_cb_t cb,
				void *data)
{
	GRil *ril = ofono_cell_info_get_data(ci);
	struct cb_data *cbd = cb_data_new(cb, data, ril);

	if (g_ril_send(ril, RIL_REQUEST_GET_CELL_INFO_LIST, NULL,
			query_cb, cbd, g_free) == 0) {
		g_free(cbd);
		CALLBACK_WITH_FAILURE(cb, NULL, data);
	}
}

static gboolean ril_delayed_register(gpointer user_data)
{
	struct ofono_cell_info *ci = user_data;
	DBG("");
	ofono_cell_info_register(ci);

	/* This makes the timeout a single-shot */
	return FALSE;
}

static int ril_cell_info_probe(struct ofono_cell_info *ci, unsigned int vendor,
				void *data)
{
	GRil *ril = NULL;

	if (data != NULL)
		ril = g_ril_clone(data);

	ofono_cell_info_set_data(ci, ril);

	/*
	 * ofono_devinfo_register() needs to be called after
	 * the driver has been set in ofono_cell_info_create(),
	 * which calls this function.  Most other drivers make
	 * some kind of capabilities query to the modem, and then
	 * call register in the callback; we use an idle event instead.
	 */
	g_idle_add(ril_delayed_register, ci);

	return 0;
}

static void ril_cell_info_remove(struct ofono_cell_info *ci)
{
	GRil *ril = ofono_cell_info_get_data(ci);

	ofono_cell_info_set_data(ci, NULL);

	g_ril_unref(ril);
}

static struct ofono_cell_info_driver driver = {
	.name			= RILMODEM,
	.probe			= ril_cell_info_probe,
	.remove			= ril_cell_info_remove,
	.query_list             = ril_cell_info_query_list
};

void ril_cell_info_init(void)
{
	ofono_cell_info_driver_register(&driver);
}

void ril_cell_info_exit(void)
{
	ofono_cell_info_driver_unregister(&driver);
}
