/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2014  Canonical Ltd. All rights reserved.
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
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>

#include <glib.h>
#include <gdbus.h>
#include <errno.h>

#include "ofono.h"

#include "common.h"
#include "powerd.h"

static GSList *g_drivers = NULL;

struct ofono_powerd {
	const struct ofono_powerd_driver *driver;
	void *driver_data;
	struct ofono_atom *atom;
	guint display_watch;
};

static void set_display_state_cb(const struct ofono_error *error, void *data)
{
	if (error->type != OFONO_ERROR_TYPE_NO_ERROR)
		DBG("request to ril failed with error: %s",
				telephony_error_to_str(error));
}

static gboolean display_state_watch(DBusConnection *conn, DBusMessage *message,
					void *user_data)
{
	int32_t state;
	uint32_t flags;
	struct ofono_powerd *powerd = user_data;

	dbus_message_get_args(message, NULL, DBUS_TYPE_INT32, &state,
				DBUS_TYPE_UINT32, &flags, DBUS_TYPE_INVALID);

	DBG("Display state is now %"PRId32", flags %"PRIu32, state, flags);

	if (powerd->driver && powerd->driver->set_display_state)
		powerd->driver->set_display_state(powerd, state,
						set_display_state_cb, powerd);

	return TRUE;
}

static void powerd_unregister(struct ofono_atom *atom)
{
	struct ofono_powerd *powerd = __ofono_atom_get_data(atom);
	DBusConnection *conn = ofono_dbus_get_connection();

	g_dbus_remove_watch(conn, powerd->display_watch);
}

static void powerd_remove(struct ofono_atom *atom)
{
	struct ofono_powerd *powerd = __ofono_atom_get_data(atom);

	DBG("atom: %p", atom);

	if (powerd == NULL)
		return;

	if (powerd->driver && powerd->driver->remove)
		powerd->driver->remove(powerd);

	g_free(powerd);
}

void ofono_powerd_register(struct ofono_powerd *powerd)
{
	DBusConnection *conn = ofono_dbus_get_connection();

	/* Listen to powerd signals */
	powerd->display_watch =
		g_dbus_add_signal_watch(conn, "com.canonical.powerd",
					"/com/canonical/powerd",
					"com.canonical.powerd",
					"DisplayPowerStateChange",
					display_state_watch, powerd, NULL);

	__ofono_atom_register(powerd->atom, powerd_unregister);
}

int ofono_powerd_driver_register(const struct ofono_powerd_driver *d)
{
	DBG("driver: %p, name: %s", d, d->name);

	if (d->probe == NULL)
		return -EINVAL;

	g_drivers = g_slist_prepend(g_drivers, (void *) d);

	return 0;
}

void ofono_powerd_driver_unregister(const struct ofono_powerd_driver *d)
{
	DBG("driver: %p, name: %s", d, d->name);

	g_drivers = g_slist_remove(g_drivers, (void *) d);
}

struct ofono_powerd *ofono_powerd_create(struct ofono_modem *modem,
						unsigned int vendor,
						const char *driver,
						void *data)
{
	struct ofono_powerd *powerd;
	GSList *l;

	if (driver == NULL)
		return NULL;

	powerd = g_try_new0(struct ofono_powerd, 1);

	if (powerd == NULL)
		return NULL;

	powerd->atom = __ofono_modem_add_atom(modem, OFONO_ATOM_TYPE_POWERD,
						powerd_remove, powerd);

	for (l = g_drivers; l; l = l->next) {
		const struct ofono_powerd_driver *drv = l->data;

		if (g_strcmp0(drv->name, driver))
			continue;

		if (drv->probe(powerd, vendor, data) < 0)
			continue;

		powerd->driver = drv;
		break;
	}

	return powerd;
}

void ofono_powerd_remove(struct ofono_powerd *powerd)
{
	__ofono_atom_free(powerd->atom);
}

void ofono_powerd_set_data(struct ofono_powerd *powerd, void *data)
{
	powerd->driver_data = data;
}

void *ofono_powerd_get_data(struct ofono_powerd *powerd)
{
	return powerd->driver_data;
}
