/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2008-2011  Intel Corporation. All rights reserved.
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
#include <glib.h>
#include <gdbus.h>

#include "ofono.h"
#include "storage.h"

#define MAX_ICCID_SIZE 20
#define MANAGER_SETTINGS "manager"
#define SETTINGS_GROUP "Settings"

struct ofono_manager {
	GKeyFile *settings;
	char iccid_voice[MAX_ICCID_SIZE + 1];
	char iccid_sms[MAX_ICCID_SIZE + 1];
	unsigned modemwatch_id;
	GList *modems;
	GHashTable *sim_hash;
	struct ofono_modem *pref_voice;
	struct ofono_modem *pref_sms;
};

struct search_data {
	struct ofono_manager *mng;
	const char *iccid;
	gboolean check_sim_ready;
	struct ofono_modem *preferred;
};

static struct ofono_manager g_manager;

static void append_modem(struct ofono_modem *modem, void *userdata)
{
	DBusMessageIter *array = userdata;
	const char *path = ofono_modem_get_path(modem);
	DBusMessageIter entry, dict;

	if (ofono_modem_is_registered(modem) == FALSE)
		return;

	dbus_message_iter_open_container(array, DBUS_TYPE_STRUCT,
						NULL, &entry);
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_OBJECT_PATH,
					&path);
	dbus_message_iter_open_container(&entry, DBUS_TYPE_ARRAY,
				OFONO_PROPERTIES_ARRAY_SIGNATURE,
				&dict);

	__ofono_modem_append_properties(modem, &dict);
	dbus_message_iter_close_container(&entry, &dict);
	dbus_message_iter_close_container(array, &entry);
}

static DBusMessage *manager_get_modems(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	DBusMessage *reply;
	DBusMessageIter iter;
	DBusMessageIter array;

	reply = dbus_message_new_method_return(msg);
	if (reply == NULL)
		return NULL;

	dbus_message_iter_init_append(reply, &iter);

	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					DBUS_STRUCT_BEGIN_CHAR_AS_STRING
					DBUS_TYPE_OBJECT_PATH_AS_STRING
					DBUS_TYPE_ARRAY_AS_STRING
					DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
					DBUS_TYPE_STRING_AS_STRING
					DBUS_TYPE_VARIANT_AS_STRING
					DBUS_DICT_ENTRY_END_CHAR_AS_STRING
					DBUS_STRUCT_END_CHAR_AS_STRING,
					&array);
	__ofono_modem_foreach(append_modem, &array);
	dbus_message_iter_close_container(&iter, &array);

	return reply;
}

static DBusMessage *manager_get_properties(DBusConnection *conn,
						DBusMessage *msg, void *data)
{
	struct ofono_manager *mng = data;
	const char *iccid_voice = mng->iccid_voice;
	const char *iccid_sms = mng->iccid_sms;
	DBusMessage *reply;
	DBusMessageIter iter;
	DBusMessageIter dict;

	reply = dbus_message_new_method_return(msg);
	if (reply == NULL)
		return NULL;

	dbus_message_iter_init_append(reply, &iter);

	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					OFONO_PROPERTIES_ARRAY_SIGNATURE,
					&dict);

	ofono_dbus_dict_append(&dict, "PreferredCardForVoice",
						DBUS_TYPE_STRING, &iccid_voice);
	ofono_dbus_dict_append(&dict, "PreferredCardForSMS",
						DBUS_TYPE_STRING, &iccid_sms);

	dbus_message_iter_close_container(&iter, &dict);

	return reply;
}

static void search_preferred_card(struct ofono_modem *modem, void *data)
{
	struct search_data *sd = data;
	struct ofono_atom *sim_atom =
			__ofono_modem_find_atom(modem, OFONO_ATOM_TYPE_SIM);
	struct ofono_sim *sim;

	/* Take first valid one */
	if (sd->preferred != NULL)
		return;

	if (sim_atom == NULL)
		return;

	sim = __ofono_atom_get_data(sim_atom);

	if (sd->check_sim_ready
			&& ofono_sim_get_state(sim) != OFONO_SIM_STATE_READY)
		return;

	if (sd->iccid == NULL || sd->iccid[0] == 0
			|| g_strcmp0(ofono_sim_get_iccid(sim), sd->iccid) == 0)
		sd->preferred = modem;
}

static DBusMessage *set_iccid_property(struct ofono_manager *mng,
					DBusMessage *msg, DBusMessageIter *var,
					const char *name, char *dest)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	const char *value, *iccid;

	if (dbus_message_iter_get_arg_type(var) != DBUS_TYPE_STRING)
		return __ofono_error_invalid_args(msg);

	dbus_message_iter_get_basic(var, &value);

	if (g_strcmp0(value, dest) == 0 || (value == NULL && dest[0] == 0))
		return dbus_message_new_method_return(msg);

	if (value == NULL || value[0] == 0) {
		dest[0] = 0;
	} else {
		struct search_data search = { mng, value, FALSE, NULL };

		/* Check if it is a valid value */
		__ofono_modem_foreach(search_preferred_card, &search);

		if (search.preferred == NULL)
			return __ofono_error_invalid_args(msg);

		strcpy(dest, value);
	}

	if (mng->settings) {
		g_key_file_set_string(mng->settings, SETTINGS_GROUP,
								name, dest);
		storage_sync(NULL, MANAGER_SETTINGS, mng->settings);
	}

	iccid = dest;
	ofono_dbus_signal_property_changed(conn, OFONO_MANAGER_PATH,
						OFONO_MANAGER_INTERFACE, name,
						DBUS_TYPE_STRING, &iccid);

	return dbus_message_new_method_return(msg);
}

static DBusMessage *manager_set_property(DBusConnection *conn,
						DBusMessage *msg, void *data)
{
	struct ofono_manager *mng = data;
	DBusMessageIter iter;
	DBusMessageIter var;
	const char *property;

	if (!dbus_message_iter_init(msg, &iter))
		return __ofono_error_invalid_args(msg);

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING)
		return __ofono_error_invalid_args(msg);

	dbus_message_iter_get_basic(&iter, &property);
	dbus_message_iter_next(&iter);

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT)
		return __ofono_error_invalid_args(msg);

	dbus_message_iter_recurse(&iter, &var);

	if (g_strcmp0(property, "PreferredCardForVoice") == 0)
		return set_iccid_property(mng, msg, &var,
						"PreferredCardForVoice",
						mng->iccid_voice);
	else if (g_strcmp0(property, "PreferredCardForSMS") == 0)
		return set_iccid_property(mng, msg, &var,
						"PreferredCardForSMS",
						mng->iccid_sms);

	return __ofono_error_invalid_args(msg);
}

static const GDBusMethodTable manager_methods[] = {
	{ GDBUS_METHOD("GetModems",
			NULL, GDBUS_ARGS({ "modems", "a(oa{sv})" }),
			manager_get_modems) },
	{ GDBUS_ASYNC_METHOD("GetProperties",
			NULL, GDBUS_ARGS({ "properties", "a{sv}" }),
			manager_get_properties) },
	{ GDBUS_ASYNC_METHOD("SetProperty",
			GDBUS_ARGS({ "property", "s" }, { "value", "v" }),
			NULL, manager_set_property) },
	{ }
};

static const GDBusSignalTable manager_signals[] = {
	{ GDBUS_SIGNAL("ModemAdded",
		GDBUS_ARGS({ "path", "o" }, { "properties", "a{sv}" })) },
	{ GDBUS_SIGNAL("ModemRemoved",
		GDBUS_ARGS({ "path", "o" })) },
	{ GDBUS_SIGNAL("PropertyChanged",
		GDBUS_ARGS({ "name", "s" }, { "value", "v" })) },
	{ }
};

static void load_iccid_setting(GKeyFile *file, const char *group,
						const char *key, char *dest)
{
	char *value = g_key_file_get_string(file, group, key, NULL);

	if (value != NULL)
		strncpy(dest, value, MAX_ICCID_SIZE);
}

struct ofono_modem *__ofono_manager_get_preferred_for_voice(void)
{
	struct search_data
		search = { &g_manager, g_manager.iccid_voice, TRUE, NULL };

	__ofono_modem_foreach(search_preferred_card, &search);

	return search.preferred;
}

struct ofono_modem *__ofono_manager_get_preferred_for_sms(void)
{
	struct search_data search = { &g_manager, g_manager.iccid_sms, NULL };

	__ofono_modem_foreach(search_preferred_card, &search);

	return search.preferred;
}

static void sim_state_watch(enum ofono_sim_state new_state, void *data)
{
	struct ofono_modem *modem = data;

	if (new_state != OFONO_SIM_STATE_READY) {
		if (g_manager.modems == NULL)
			return;

		g_manager.modems = g_list_remove(g_manager.modems, modem);

		return;
	}

	if (__ofono_modem_find_atom(modem, OFONO_ATOM_TYPE_VOICECALL) == NULL)
		return;

	g_manager.modems = g_list_append(g_manager.modems, modem);
}

static gboolean sim_watch_remove(gpointer key, gpointer value,
				gpointer user_data)
{
	struct ofono_sim *sim = key;

	ofono_sim_remove_state_watch(sim, GPOINTER_TO_UINT(value));

	return TRUE;
}

static void sim_watch(struct ofono_atom *atom,
			enum ofono_atom_watch_condition cond, void *data)
{
	struct ofono_sim *sim = __ofono_atom_get_data(atom);
	struct ofono_modem *modem = data;
	unsigned watch;

	if (cond == OFONO_ATOM_WATCH_CONDITION_UNREGISTERED) {
		sim_state_watch(OFONO_SIM_STATE_NOT_PRESENT, modem);

		sim_watch_remove(sim,
				g_hash_table_lookup(g_manager.sim_hash, sim),
				NULL);
		g_hash_table_remove(g_manager.sim_hash, sim);

		return;
	}

	watch = ofono_sim_add_state_watch(sim, sim_state_watch, modem, NULL);
	g_hash_table_insert(g_manager.sim_hash, sim, GUINT_TO_POINTER(watch));
	sim_state_watch(ofono_sim_get_state(sim), modem);
}

static void modem_watch(struct ofono_modem *modem, gboolean added, void *user)
{
	DBG("modem: %p, added: %d", modem, added);

	if (added == FALSE)
		return;

	__ofono_modem_add_atom_watch(modem, OFONO_ATOM_TYPE_SIM,
							sim_watch, modem, NULL);
}

int __ofono_manager_init(void)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	gboolean ret;

	ret = g_dbus_register_interface(conn, OFONO_MANAGER_PATH,
					OFONO_MANAGER_INTERFACE,
					manager_methods, manager_signals,
					NULL, &g_manager, NULL);

	if (ret == FALSE)
		return -1;

	g_manager.sim_hash = g_hash_table_new(g_direct_hash, g_direct_equal);
	g_manager.modemwatch_id =
			__ofono_modemwatch_add(modem_watch, &g_manager, NULL);

	g_manager.settings = storage_open(NULL, MANAGER_SETTINGS);
	if (g_manager.settings == NULL) {
		ofono_warn("Cannot open manager settings file");
		return 0;
	}

	load_iccid_setting(g_manager.settings, SETTINGS_GROUP,
				"PreferredCardForVoice", g_manager.iccid_voice);
	load_iccid_setting(g_manager.settings, SETTINGS_GROUP,
				"PreferredCardForSMS", g_manager.iccid_sms);

	return 0;
}

void __ofono_manager_cleanup(void)
{
	DBusConnection *conn = ofono_dbus_get_connection();

	/*
	 * TODO Maybe clear settings if the value is not from a currently
	 * present card?
	 */

	if (g_manager.settings)
		storage_close(NULL, MANAGER_SETTINGS,
						g_manager.settings, FALSE);

	__ofono_modemwatch_remove(g_manager.modemwatch_id);

	g_list_free(g_manager.modems);
	g_hash_table_foreach_remove(g_manager.sim_hash, sim_watch_remove, NULL);
	g_hash_table_destroy(g_manager.sim_hash);

	g_dbus_unregister_interface(conn, OFONO_MANAGER_PATH,
					OFONO_MANAGER_INTERFACE);
}
