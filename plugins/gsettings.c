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

#define GSETTINGS_PHONE_SCHEMA "com.ubuntu.phone"
#define HOME_ENV "HOME"

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

static void set_process_id(gid_t gid, uid_t uid)
{
	if (setegid(gid) < 0)
		ofono_error("%s: setegid(%d) failed: %s (%d)",
				__func__, gid, strerror(errno), errno);

	if (seteuid(uid) < 0)
		ofono_error("%s: seteuid(%d) failed: %s (%d)",
				__func__, uid, strerror(errno), errno);
}

static GSettings *get_user_gsettings(char **old_home)
{
	GSettings *settings = NULL;
	struct passwd *pw;
	int res;
	uid_t uid;

	uid = get_active_seat_uid();

	errno = 0;
	pw = getpwuid(uid);
	if (pw == NULL) {
		ofono_error("Cannot retrieve user data: %s (%d)",
							strerror(errno), errno);
		goto end;
	}

	set_process_id(pw->pw_gid, uid);

	*old_home = g_strdup(getenv(HOME_ENV));

	res = setenv(HOME_ENV, pw->pw_dir ? pw->pw_dir : "", TRUE);
	if (res < 0)
		ofono_error("Cannot set home: %s (%d)", strerror(errno), errno);

	DBG("home is %s; home for %s is %s", PRINTABLE_STR(*old_home),
			PRINTABLE_STR(pw->pw_name), PRINTABLE_STR(pw->pw_dir));

	settings = g_settings_new(GSETTINGS_PHONE_SCHEMA);
	if (settings == NULL) {
		ofono_error("Schema %s not found", GSETTINGS_PHONE_SCHEMA);
		g_free(*old_home);
		set_process_id(0, 0);
	}

end:
	return settings;
}

static void release_user_gsettings(GSettings *settings, char *old_home)
{
	g_object_unref(settings);

	if (old_home != NULL) {
		setenv(HOME_ENV, old_home, TRUE);
		g_free(old_home);
	}

	set_process_id(0, 0);
}

static const char *translate_name(const char *name)
{
	if (strcmp(name, PREFERRED_VOICE_MODEM) == 0)
		return "default-sim-for-calls";

	return name;
}

static char *gsettings_get_setting_str(const char *name)
{
	GSettings *settings;
	char *old_home;
	char *value = NULL;

	settings = get_user_gsettings(&old_home);
	if (settings == NULL)
		goto end;

	value = g_settings_get_string(settings, translate_name(name));
	if (value == NULL)
		ofono_error("gsettings value for %s not found", name);

	release_user_gsettings(settings, old_home);

end:
	return value;
}

static struct ofono_system_settings_driver gsettings_driver = {
	.name	 = "GSettings System Settings",
	.get_setting_str = gsettings_get_setting_str
};

static int gsettings_init(void)
{
	return ofono_system_settings_driver_register(&gsettings_driver);
}

static void gsettings_exit(void)
{
	ofono_system_settings_driver_unregister(&gsettings_driver);
}

OFONO_PLUGIN_DEFINE(gsettings, "GSettings System Settings Plugin", VERSION,
			OFONO_PLUGIN_PRIORITY_DEFAULT,
			gsettings_init, gsettings_exit)
