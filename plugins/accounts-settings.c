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

#include <sys/types.h>
#include <pwd.h>
#include <errno.h>
#include <unistd.h>

#include <glib.h>
#include <gio/gio.h>
#include <systemd/sd-login.h>

#define OFONO_API_SUBJECT_TO_CHANGE
#include <ofono/types.h>
#include <ofono/log.h>
#include <ofono/plugin.h>
#include <ofono/system-settings.h>

#define PRINTABLE_STR(s) ((s) ? (s) : "(null)")

//static GHashTable *settings_;

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

static void get_user_account_setting(void)
{
	struct passwd *pw;
	uid_t uid;

	uid = get_active_seat_uid();

	errno = 0;
	pw = getpwuid(uid);
	if (pw == NULL) {
		ofono_error("Cannot retrieve user data: %s (%d)",
							strerror(errno), errno);
		goto end;
	}

	// TODO Access accountsservice

end:
	return;
}

/*
static const char *translate_name(const char *name)
{
	if (strcmp(name, PREFERRED_VOICE_MODEM) == 0)
		return "DefaultSimForCalls";

	return name;
}*/

static char *accounts_settings_get_setting_str(const char *name)
{
	char *value = NULL;

	get_user_account_setting();

	return value;
}

static struct ofono_system_settings_driver accounts_settings_driver = {
	.name	 = "Accounts Service System Settings",
	.get_setting_str = accounts_settings_get_setting_str
};

static int accounts_settings_init(void)
{
	return ofono_system_settings_driver_register(&accounts_settings_driver);
}

static void accounts_settings_exit(void)
{
	ofono_system_settings_driver_unregister(&accounts_settings_driver);
}

OFONO_PLUGIN_DEFINE(accounts_settings,
			"Accounts Service System Settings Plugin",
			VERSION, OFONO_PLUGIN_PRIORITY_DEFAULT,
			accounts_settings_init, accounts_settings_exit)
