/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* test_conduit.c
 *
 * Copyright (C) 2001  Helix Code, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: JP Rosevear
 */

#include <gnome-pilot-conduit.h>
#include <gnome-pilot-conduit-standard.h>

#define CONDUIT_VERSION "0.0.1"
#ifdef G_LOG_DOMAIN
#undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "testconduit"

#define INFO(e) g_log (G_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, e)

GnomePilotConduit * conduit_get_gpilot_conduit (guint32);
void conduit_destroy_gpilot_conduit (GnomePilotConduit*);

static gint 
copy_to_pilot(GnomePilotConduit *c, GnomePilotDBInfo *dbi)
{
	INFO ("Called copy_to_remote");
	return 0;
}

static gint 
copy_from_pilot(GnomePilotConduit *c, GnomePilotDBInfo *dbi)
{
	INFO("Called copy_from_remote");
	return 0;
}

static gint 
merge_to_pilot(GnomePilotConduit *c, GnomePilotDBInfo *dbi)
{
	INFO("Called merge_to_remote");
	return 0;
}

static gint 
merge_from_pilot(GnomePilotConduit *c, GnomePilotDBInfo *dbi)
{
	INFO("Called merge_from_remote");
	return 0;
}

static gint 
synchronize(GnomePilotConduit *c, GnomePilotDBInfo *dbi)
{
	INFO("synchronize called");

	if (GNOME_PILOT_CONDUIT_STANDARD (c)->slow)
		INFO("  Using slow sync");
	else
		INFO("  Using fast sync");

	return 0;
}

GnomePilotConduit *
conduit_get_gpilot_conduit(guint32 pilotId) 
{
	GObject *retval;

	retval = gnome_pilot_conduit_standard_new ("MailDB",0x6d61696c, NULL);
	g_assert(retval != NULL);

	g_signal_connect(retval, "copy_from_pilot", G_CALLBACK(copy_from_pilot), NULL);
	g_signal_connect(retval, "copy_to_pilot", G_CALLBACK(copy_to_pilot), NULL);
	g_signal_connect(retval, "merge_to_pilot", G_CALLBACK(merge_to_pilot), NULL);
	g_signal_connect(retval, "merge_from_pilot", G_CALLBACK(merge_from_pilot), NULL);
	g_signal_connect(retval, "synchronize", G_CALLBACK(synchronize), NULL);

	return GNOME_PILOT_CONDUIT(retval); 
}


void 
conduit_destroy_gpilot_conduit(GnomePilotConduit *c) 
{
	g_object_unref(G_OBJECT(c));
}

