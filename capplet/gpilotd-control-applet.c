/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gpilotd-control-applet.c
 *
 * Copyright (C) 1998 Red Hat Software       
 * Copyright (C) 1999-2000 Free Software Foundation
 * Copyright (C) 2001  Ximian, Inc.
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
 * Authors: Eskil Heyn Olsen
 *          Vadim Strizhevsky
 *          Michael Fulbright <msf@redhat.com>
 *          JP Rosevear <jpr@ximian.com>
 *
 */

#include <config.h>
#include <ctype.h>

#include <gnome-pilot-client.h>

#include "gnome-pilot-assistant.h"
#include "gnome-pilot-capplet.h"

#include "pilot.h"
#include "util.h"

static void
monitor_pilots (GnomePilotClient *gpc, PilotState * state)
{
	GList *tmp = state->pilots;
	if (tmp!= NULL){
		while (tmp!= NULL){
			GPilotPilot *pilot =(GPilotPilot*)tmp->data;
			g_message ("pilot = %s",pilot->name);
			tmp = tmp->next;
		}
	}
}

static GMainLoop *main_loop = NULL;

static void
response_cb (GtkDialog *dialog, gint response_id)
{
	if (response_id == GTK_RESPONSE_HELP) {
		gtk_show_uri (GTK_WINDOW (dialog), "ghelp:gnome-pilot#conftool-pilots", GDK_CURRENT_TIME);
	} else {
		g_main_loop_quit (main_loop);
	}
}

int
main (int argc, char *argv[])
{
	GnomePilotClient *gpc = NULL;
	PilotState *state = NULL;
	GnomePilotCapplet *gpcap = NULL;
	gboolean assistant_on = FALSE, assistant_prog = FALSE;

	GOptionEntry options[] = {
		{"assistant", '\0', 0, G_OPTION_ARG_NONE, &assistant_on, "Start assistant only", NULL}, /* FIXME: Make N_() translatable after string freeze */
		
		{NULL}
	};

	GError *err = NULL;
	GOptionContext *context;

	bindtextdomain (PACKAGE, GNOMELOCALEDIR);
	textdomain (PACKAGE);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	context = g_option_context_new (_(" - configure gnome-pilot settings"));
	g_option_context_add_main_entries (context, options, PACKAGE);
	/* GTK4: gtk_get_option_group removed; gtk_init() is called separately */
	if (!g_option_context_parse (context, &argc, &argv, &err)) {
		g_error (_("Error initializing gpilotd capplet : %s"), err->message);
		exit (1);
	}
	g_option_context_free (context);

	gtk_init ();

	if (assistant_on) {
		assistant_on = TRUE;
		assistant_prog = TRUE;
	}
	gtk_window_set_default_icon_name ("gnome-palm");
	
	/* put all code to set things up in here */
	if (loadPilotState (&state) < 0) {
		error_dialog (NULL, _("Error loading PDA state, aborting"));
		g_error (_("Error loading PDA state, aborting"));
		return -1;
	}

	gpc = GNOME_PILOT_CLIENT (gnome_pilot_client_new ());

	if (gnome_pilot_client_connect_to_daemon (gpc) != GPILOTD_OK) {
		error_dialog (NULL,_("Cannot connect to the GnomePilot Daemon"));
		g_error (_("Cannot connect to the GnomePilot Daemon"));
		return -1;
	}

	monitor_pilots (gpc, state);
	if (assistant_on) {
		GObject *assistant;
		
		if (state->pilots!= NULL || state->devices!= NULL) {
			error_dialog (NULL, "Cannot run assistant if PDAs or devices already configured"); /* FIXME: Make translatable after string freeze */
			return -1;
		}

		assistant = gnome_pilot_assistant_new (gpc);
		gnome_pilot_assistant_run_and_close (GNOME_PILOT_ASSISTANT (assistant));
	} else {
		gboolean assistant_finished = TRUE;
		
		gpcap = gnome_pilot_capplet_new (gpc);

		/* quit when the Close button is clicked on our dialog */
		g_signal_connect (G_OBJECT (gpcap), "response", G_CALLBACK (response_cb), NULL);

		/* popup the assistant if nothing is configured - assume this is the first time */
		if (state->pilots == NULL && state->devices == NULL) {
			GObject *assistant;
			
			assistant = gnome_pilot_assistant_new (gpc);
			assistant_finished = gnome_pilot_assistant_run_and_close (GNOME_PILOT_ASSISTANT (assistant));

			if (gpcap != NULL)
				gnome_pilot_capplet_update (GNOME_PILOT_CAPPLET (gpcap));
		}

		if (assistant_finished) {
			main_loop = g_main_loop_new (NULL, FALSE);
			gtk_window_present (GTK_WINDOW (gpcap));
			g_main_loop_run (main_loop);
			g_main_loop_unref (main_loop);
		}		
	}
	freePilotState (state);

	return 0;
}    

