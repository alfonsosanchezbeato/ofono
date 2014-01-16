/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2009-2010 Nokia Corporation and/or its subsidiary(-ies).
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

#include <errno.h>

#include <glib.h>
#include <gdbus.h>

#include "ofono.h"

#include "common.h"
#include "ofono/cell-info.h"


struct ofono_cell_info {
	DBusMessage *pending;
	struct ofono_atom *atom;
	const struct ofono_cell_info_driver *driver;
	void *driver_data;
};


static DBusMessage *ci_get_cells(DBusConnection *, DBusMessage *, void *);

static GSList *g_drivers = NULL;

static GDBusMethodTable ci_methods[] = {
	{ "AquireMeasurement",	      "",	"aa{sv}",	ci_get_cells,
	  G_DBUS_METHOD_FLAG_ASYNC },
	{ }
};

static GDBusSignalTable ci_signals[] = {
	{ }
};

int ofono_cell_info_driver_register(struct ofono_cell_info_driver *driver)
{
	DBG("driver: %p, name: %s", driver, driver->name);

	if (driver->probe == NULL)
		return -EINVAL;

	g_drivers = g_slist_prepend(g_drivers, (void *) driver);

	return 0;
}

void ofono_cell_info_driver_unregister(struct ofono_cell_info_driver *driver)
{
	DBG("driver: %p, name: %s", driver, driver->name);

	g_drivers = g_slist_remove(g_drivers, (void *) driver);
}

void ofono_cell_info_remove(struct ofono_cell_info *ci)
{
	__ofono_atom_free(ci->atom);
}

static void cell_info_unregister(struct ofono_atom *atom)
{
	struct ofono_cell_info *ci = __ofono_atom_get_data(atom);
	const char *path = __ofono_atom_get_path(ci->atom);
	DBusConnection *conn = ofono_dbus_get_connection();
	struct ofono_modem *modem = __ofono_atom_get_modem(ci->atom);

	ofono_modem_remove_interface(modem, OFONO_CELL_INFO_INTERFACE);

	if (!g_dbus_unregister_interface(conn, path, OFONO_CELL_INFO_INTERFACE))
		ofono_error("Failed to unregister interface %s",
				OFONO_CELL_INFO_INTERFACE);
}

static void cell_info_remove(struct ofono_atom *atom)
{
	struct ofono_cell_info *ci = __ofono_atom_get_data(atom);
	DBG("atom: %p", atom);

	if (ci == NULL)
		return;

	if (ci->driver && ci->driver->remove)
		ci->driver->remove(ci);

	g_free(ci);
}

void ofono_cell_info_register(struct ofono_cell_info *ci)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	struct ofono_modem *modem = __ofono_atom_get_modem(ci->atom);
	const char *path = __ofono_atom_get_path(ci->atom);

	DBG("Modem: %p", modem);

	if (!g_dbus_register_interface(conn, path,
					OFONO_CELL_INFO_INTERFACE,
					ci_methods, ci_signals, NULL,
					ci, NULL)) {
		ofono_error("Could not create %s interface",
				OFONO_CELL_INFO_INTERFACE);

		return;
	}

	ofono_modem_add_interface(modem, OFONO_CELL_INFO_INTERFACE);

	__ofono_atom_register(ci->atom, cell_info_unregister);
}

struct ofono_cell_info *ofono_cell_info_create(struct ofono_modem *modem,
						unsigned int vendor,
						const char *driver,
						void *data)
{
	struct ofono_cell_info *ci;
	GSList *l;

	if (driver == NULL)
		return NULL;

	ci = g_try_new0(struct ofono_cell_info, 1);
	if (ci == NULL)
		return NULL;

	ci->atom = __ofono_modem_add_atom(modem, OFONO_ATOM_TYPE_CELL_INFO,
						cell_info_remove,
						ci);

	for (l = g_drivers; l; l = l->next) {
		const struct ofono_cell_info_driver *drv = l->data;

		if (g_strcmp0(drv->name, driver))
			continue;

		if (drv->probe(ci, vendor, data) < 0)
			continue;

		ci->driver = drv;
		break;
	}

	return ci;
}

void *ofono_cell_info_get_data(struct ofono_cell_info *ci)
{
	return ci->driver_data;
}

void ofono_cell_info_set_data(struct ofono_cell_info *ci, void *cid)
{
	ci->driver_data = cid;
}

static int append_geran_meta_data(DBusMessageIter *iter,
	     struct ofono_cell_info_results *ci)
{
	DBusMessageIter iter_array;
	const char *type = "GERAN";
	const char *mcc = ci->mcc;
	const char *mnc = ci->mnc;

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
						"{sv}",
						&iter_array);

	ofono_dbus_dict_append(&iter_array, "Type",
				DBUS_TYPE_STRING,
				&type);

	ofono_dbus_dict_append(&iter_array, "MobileNetworkCode",
				DBUS_TYPE_STRING,
				&mnc);

	ofono_dbus_dict_append(&iter_array, "MobileCountryCode",
				DBUS_TYPE_STRING,
				&mcc);
	ofono_dbus_dict_append(&iter_array, "LocationAreaCode",
				DBUS_TYPE_UINT16,
				&ci->geran.lac);
	ofono_dbus_dict_append(&iter_array, "CellId",
				DBUS_TYPE_UINT16,
				&ci->geran.ci);

      if (ci->geran.ta != OFONO_CI_FIELD_TA_UNDEFINED)
	      ofono_dbus_dict_append(&iter_array, "TimingAdvance",
					DBUS_TYPE_BYTE,
					&ci->geran.ta);

      dbus_message_iter_close_container(iter, &iter_array);

      return 0;

}

static void add_geran_neighbor(DBusMessageIter *iter,
	     struct geran_neigh_cell *cell)
{
	DBusMessageIter iter_array;

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
						"{sv}",
						&iter_array);
	ofono_dbus_dict_append(&iter_array,
				"AbsoluteRadioFrequencyChannelNumber",
				DBUS_TYPE_UINT16,
				&cell->arfcn);

	ofono_dbus_dict_append(&iter_array, "BaseStationIdentityCode",
				DBUS_TYPE_BYTE,
				&cell->bsic);
	ofono_dbus_dict_append(&iter_array, "RXLEV",
				DBUS_TYPE_BYTE,
				&cell->rxlev);

	dbus_message_iter_close_container(iter, &iter_array);
}

static int fill_geran_ci(DBusMessageIter *iter,
	      struct ofono_cell_info_results *ci)
{
	int i;

	append_geran_meta_data(iter, ci);

	for (i = 0; i < ci->geran.no_cells; ++i)
		add_geran_neighbor(iter, &ci->geran.nmr[i]);

	return 0;
}

static int append_utra_neigh_cell_data(DBusMessageIter *iter,
	    struct cell_measured_results *cmr)
{
	ofono_dbus_dict_append(iter, "ScramblingCode",
				DBUS_TYPE_UINT16,
				&cmr->sc);

	if (cmr->ucid != OFONO_CI_FIELD_UCID_UNDEFINED)
		ofono_dbus_dict_append(iter, "UniqueCellId",
					DBUS_TYPE_UINT32,
					&cmr->ucid);

	if (cmr->ecn0 != OFONO_CI_FIELD_ECN0_UNDEFINED)
		ofono_dbus_dict_append(iter, "CPICH-ECN0",
					DBUS_TYPE_BYTE,
					&cmr->ecn0);

	if (cmr->rscp != OFONO_CI_FIELD_RSCP_UNDEFINED)
		ofono_dbus_dict_append(iter, "CPICH-RSCP",
					DBUS_TYPE_INT16,
					&cmr->rscp);

	if (cmr->pathloss != OFONO_CI_FIELD_PATHLOSS_UNDEFINED)
		ofono_dbus_dict_append(iter, "Pathloss",
					DBUS_TYPE_BYTE,
					&cmr->pathloss);

	return 0;
}

static int append_utra_neigh_freq_data(DBusMessageIter *iter,
	    struct measured_results_list *mrl)
{
	ofono_dbus_dict_append(iter, "ReceivedSignalStrengthIndicator",
				DBUS_TYPE_BYTE,
				&mrl->rssi);

	ofono_dbus_dict_append(iter, "UARFCN-DL",
				DBUS_TYPE_UINT16,
				&mrl->dl_freq);

	if (mrl->ul_freq != OFONO_CI_FIELD_FREQ_UNDEFINED)
		ofono_dbus_dict_append(iter, "UARFCN-UL",
					DBUS_TYPE_UINT16,
					&mrl->ul_freq);

	return 0;
}

static int append_utra_neighbors(DBusMessageIter *iter,
	    					  struct utran *utran)
{
	DBusMessageIter iter_array;
	int i, j;

	for (i = 0; i < utran->no_freq; ++i) {

		for (j = 0; j < utran->mrl[i].no_cells; ++j) {

			dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
								"{sv}",
								&iter_array);
			append_utra_neigh_freq_data(&iter_array,
							&utran->mrl[i]);
			append_utra_neigh_cell_data(&iter_array,
							&utran->mrl[i].cmr[j]);

			dbus_message_iter_close_container(iter,
								&iter_array);
		}
	}

	return 0;
}

static void append_utran_meta_data(DBusMessageIter *iter,
	     struct ofono_cell_info_results *ci)
{
	DBusMessageIter iter_array;
	const char *type = "UTRA-FDD";
	const char *mcc = ci->mcc;
	const char *mnc = ci->mnc;

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
						"{sv}",
						&iter_array);

	ofono_dbus_dict_append(&iter_array, "Type",
				DBUS_TYPE_STRING,
				&type);
	ofono_dbus_dict_append(&iter_array, "MobileNetworkCode",
				DBUS_TYPE_STRING,
				&mnc);
    ofono_dbus_dict_append(&iter_array, "MobileCountryCode",
				DBUS_TYPE_STRING,
				&mcc);
     ofono_dbus_dict_append(&iter_array, "UniqueCellId",
				DBUS_TYPE_UINT32,
				&ci->utran.ucid);
     ofono_dbus_dict_append(&iter_array, "ScramblingCode",
				DBUS_TYPE_UINT16,
				&ci->utran.sc);
     ofono_dbus_dict_append(&iter_array, "UARFCN-DL",
				DBUS_TYPE_UINT16,
				&ci->utran.dl_freq);

     if (ci->utran.ul_freq != OFONO_CI_FIELD_FREQ_UNDEFINED) {
	     ofono_dbus_dict_append(&iter_array, "UARFCN-UL",
					DBUS_TYPE_UINT16,
					&ci->utran.ul_freq);
     }

     dbus_message_iter_close_container(iter, &iter_array);
}

static int fill_utra_ci(DBusMessageIter *iter,
			struct ofono_cell_info_results *ci)
{

	append_utran_meta_data(iter, ci);
	append_utra_neighbors(iter, &ci->utran);

	return 0;
}

static void ofono_neigh_cell_info_query_cb(const struct ofono_error *error,
				struct ofono_cell_info_results *ci_results,
				void *data)
{
	struct ofono_cell_info *ci = data;
	DBusMessage *msg = ci->pending;
	DBusMessage *reply;
	DBusMessageIter iter, iter_array;
	int status;

	DBG("");

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		ofono_error("Neighbor cell info query failed");
		goto error;
	}

	reply = dbus_message_new_method_return(msg);
	if (reply == NULL) {
		ofono_error("Failed to create response");
		goto error;
	}

	dbus_message_iter_init_append(reply, &iter);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
						"a{sv}",
						&iter_array);

	if (ci_results->has_geran_cells == TRUE) {
		DBG("Geran cell info received");

		status = fill_geran_ci(&iter_array, ci_results);
		if (status != 0) {
			ofono_error("Failed to fill geran info");
			goto error;
		}
	}

	if (ci_results->has_utran_cells) {
		DBG("Utran cell info received");

		status = fill_utra_ci(&iter_array, ci_results);
		if (status != 0) {
			ofono_error("Failed to fill utran info");
			goto error;
		}
	}

	dbus_message_iter_close_container(&iter, &iter_array);
	__ofono_dbus_pending_reply(&msg, reply);
	return;

error:
	reply = __ofono_error_failed(msg);
	__ofono_dbus_pending_reply(&msg, reply);
	return;p

}

static DBusMessage *ci_get_cells(DBusConnection *conn, DBusMessage *msg,
					void *data)
{
	struct ofono_cell_info *ci = data;
	DBG("");

	ci->pending = dbus_message_ref(msg);
	ci->driver->query(ci, ofono_neigh_cell_info_query_cb, ci->driver_data);

	return NULL;
}
