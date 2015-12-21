/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C)  2015 Canonical Ltd.
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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <sys/types.h>
#include <pwd.h>
#include <errno.h>
#include <unistd.h>

#include <glib.h>
#include <gdbus.h>
#include <systemd/sd-login.h>

#include <ofono.h>

#define OFONO_API_SUBJECT_TO_CHANGE
#include <ofono/types.h>
#include <ofono/log.h>
#include <ofono/plugin.h>
#include <ofono/system-settings.h>

#define ACCOUNTS_SERVICE          "org.freedesktop.Accounts"
#define ACCOUNTS_PATH             "/org/freedesktop/Accounts/User"
#define ACCOUNTS_PHONE_INTERFACE  "com.ubuntu.touch.AccountsService.Phone"
#define DBUS_PROPERTIES_INTERFACE "org.freedesktop.DBus.Properties"

#define PRINTABLE_STR(s) ((s) ? (s) : "(null)")

struct setting_key {
	uid_t uid;
	char *name;
};

static struct setting_key *create_setting_key(uid_t uid, const char *name)
{
	struct setting_key *k = g_malloc0(sizeof(*k));
	k->uid = uid;
	k->name = g_strdup(name);
	return k;
}

static void free_setting_key(gpointer key)
{
	struct setting_key *k = key;
	g_free(k->name);
	g_free(k);
}

static guint hash_setting_key(gconstpointer key)
{
	const struct setting_key *k = key;
	guint h;

	/* Not greatest hash function ever, but probably good enough */
	h = g_str_hash(k->name);
	return h ^ (guint) k->uid;
}

static gboolean equal_setting_keys(gconstpointer a, gconstpointer b)
{
	const struct setting_key *ka = a;
	const struct setting_key *kb = b;

	if (ka->uid == kb->uid && strcmp(ka->name, ka->name) == 0)
		return TRUE;

	return FALSE;
}

static GHashTable *settings_table;

static uid_t get_active_seat_uid(void)
{
	char **seats, **iter;
	int res;
	gboolean found = FALSE;
	uid_t uid;

	res = sd_get_seats(&seats);
	if (res < 0) {
		ofono_error("Error retrieving seats: %s (%d)",
							strerror(-res), -res);
		goto end;
	} else if (res == 0) {
		ofono_info("No seats found");
		goto end;
	}

	for (iter = seats; *iter; ++iter) {

		if (!found && sd_seat_get_active(*iter, NULL, &uid) >= 0) {
			DBG("seat %s with uid %d", *iter, uid);
			found = TRUE;
		}

		free(*iter);
	}

	free(seats);

end:
	if (!found)
		uid = geteuid();

	return uid;
}

static void get_value(DBusMessage *reply)
{
	DBusMessageIter iter, val;
	char *ptr;

	DBG("");

	if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
		ofono_error("%s: ERROR reply to Get('Percentage')", __func__);
		goto done;
	}

	if (dbus_message_iter_init(reply, &iter) == FALSE) {
		ofono_error("%s: error initializing array iter", __func__);
		goto done;
	}

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT) {
		ofono_error("%s: type != VARIANT!", __func__);
		goto done;
	}

	dbus_message_iter_recurse(&iter, &val);

	if (dbus_message_iter_get_arg_type(&val) != DBUS_TYPE_STRING) {
		ofono_error("%s: type != STRING!", __func__);
		goto done;
	}

	dbus_message_iter_get_basic(&val, &ptr);

	DBG("VAL %s", ptr);

done:
	if (reply)
		dbus_message_unref(reply);
}

static GVariant *get_account_property(uid_t uid, const char *name)
{
	char path[sizeof(ACCOUNTS_PATH) + 32];
	DBusMessageIter iter;
	DBusMessage *msg, *reply;
	const char *iface = ACCOUNTS_PHONE_INTERFACE;
	DBusError *error = NULL;
	DBusConnection *connection = ofono_dbus_get_connection();

	snprintf(path, sizeof(path), ACCOUNTS_PATH"%u", (unsigned) uid);
	msg = dbus_message_new_method_call(ACCOUNTS_SERVICE, path,
						DBUS_PROPERTIES_INTERFACE,
						"Get");
	if (msg == NULL) {
		ofono_error("%s: dbus_message_new_method failed", __func__);
		return NULL;
	}

	dbus_message_iter_init_append(msg, &iter);
	dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &iface);
	dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &name);

	reply = dbus_connection_send_with_reply_and_block(connection, msg, 5000, error);
	if (reply == NULL) {
		ofono_error("%s: Sending Get failed", __func__);
		goto done;
	}

	get_value(reply);

done:
	dbus_message_unref(msg);

	return NULL;
}

static GVariant *get_user_account_setting(const char *name)
{
	uid_t uid;
	struct setting_key *key;
	GVariant *val = NULL;
	gboolean found;

	uid = get_active_seat_uid();
	key = create_setting_key(uid, name);

	found = g_hash_table_lookup_extended(settings_table, key,
							NULL, (void **) &val);
	if (!found) {
		DBG("Asking AccountsService for %s", name);
		// TODO Access accountsservice
		val = get_account_property(uid, name);
	}

	free_setting_key(key);

	return val;
}

static const char *translate_name(const char *name)
{
	if (strcmp(name, PREFERRED_VOICE_MODEM) == 0)
		return "DefaultSimForCalls";

	return name;
}

static char *accounts_settings_get_setting_str(const char *name)
{
	char *value = NULL;
	GVariant *val;

	val = get_user_account_setting(translate_name(name));
	if (val == NULL)
		goto end;

	if (!g_variant_is_of_type(val, G_VARIANT_TYPE_STRING))
		goto end;

	value = g_variant_dup_string(val, NULL);
	DBG("Name %s found, value %s", name, value);

end:
	return value;
}

static struct ofono_system_settings_driver accounts_settings_driver = {
	.name	 = "Accounts Service System Settings",
	.get_setting_str = accounts_settings_get_setting_str
};

static int accounts_settings_init(void)
{
	settings_table = g_hash_table_new_full(hash_setting_key,
				equal_setting_keys, free_setting_key, g_free);

	return ofono_system_settings_driver_register(&accounts_settings_driver);
}

static void accounts_settings_exit(void)
{
	ofono_system_settings_driver_unregister(&accounts_settings_driver);

	g_hash_table_destroy(settings_table);
}

OFONO_PLUGIN_DEFINE(accounts_settings,
			"Accounts Service System Settings Plugin",
			VERSION, OFONO_PLUGIN_PRIORITY_DEFAULT,
			accounts_settings_init, accounts_settings_exit)
