/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- *//* 
 * Copyright (C) 1998-2000 Free Software Foundation
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
 */

#include "config.h"
#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>

#include <gnome-pilot-client.h>

GnomePilotClient *gpc;
GSList *handles;
GSList *failed,*notfailed;
int handle;
GtkWidget *dialog;


gboolean now = FALSE, later = FALSE;
char *debug_modules = NULL;
gchar *pilot_arg=NULL;
gchar **filenames;

static GOptionEntry options[] = {
	{"now", 'n', 0, G_OPTION_ARG_NONE, &now, N_("Install immediately"), NULL},
	{"later", 'l', 0, G_OPTION_ARG_NONE, &later, N_("Install delayed"), NULL},
	{"pilot", 'p', 0, G_OPTION_ARG_STRING, &pilot_arg, N_("PDA to install to"), N_("PDA")},
	{G_OPTION_REMAINING, '\0', 0, G_OPTION_ARG_FILENAME_ARRAY, &filenames, N_("[FILE...]"), N_("list of files")},
	{NULL}
};

static void
gpilotd_request_completed (GnomePilotClient *gpc, gchar *pilot_id, gint handle, gpointer data)
{
	g_message ("%s completed %d", pilot_id, handle);
	handles = g_slist_remove (handles,GINT_TO_POINTER(handle));
	if (handles == NULL) {
		gtk_window_destroy (GTK_WINDOW (dialog));
		dialog = NULL;
	}
}

static void
on_warning_dialog_response (GtkDialog *dlg, gint response_id, gpointer user_data)
{
	GMainLoop *loop = (GMainLoop *) user_data;
	g_main_loop_quit (loop);
}

static void
show_warning_dialog (gchar *mesg,...)
{
	char *tmp;
	va_list ap;
	GMainLoop *loop;
	va_start (ap, mesg);

	tmp = g_strdup_vprintf (mesg, ap);
	dialog = gtk_message_dialog_new (NULL, 0,GTK_MESSAGE_WARNING,
	    GTK_BUTTONS_OK, "%s", tmp);

	loop = g_main_loop_new (NULL, FALSE);
	g_signal_connect (dialog, "response", G_CALLBACK (on_warning_dialog_response), loop);
	gtk_window_present (GTK_WINDOW (dialog));
	g_main_loop_run (loop);
	g_main_loop_unref (loop);

	gtk_window_destroy (GTK_WINDOW (dialog));
	dialog = NULL;
	g_free (tmp);
	va_end (ap);
}

static void
on_install_dialog_response (GtkDialog *dlg, gint response_id, gpointer user_data)
{
	gint *out_response = (gint *) user_data;
	*out_response = response_id;
}

int
main (int argc, char *argv[])
{
	int err, i;
	GNOME_Pilot_Survival survive;
	GError *error;
	GList *pilots = NULL;
	GOptionContext *option_context;
        
	bindtextdomain (PACKAGE, GNOMELOCALEDIR);
	textdomain (PACKAGE);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	if (argc<2) {
		g_message ("usage : %s [--now|--later] [--pilot PDA] [FILE ...]", argv[0]);
		exit (1);
	}

	option_context = g_option_context_new (PACKAGE);
	g_option_context_add_main_entries (option_context, options, NULL);
	if (!g_option_context_parse(option_context, &argc, &argv, &error)) {
		g_error (_("Error parsing commandline arguments: %s"), error->message);
		exit (1);
	}

	gtk_init ();

	gpc = GNOME_PILOT_CLIENT (gnome_pilot_client_new ());
	g_object_ref_sink (G_OBJECT (gpc));
	g_signal_connect (G_OBJECT (gpc),"completed_request", G_CALLBACK(gpilotd_request_completed), NULL);
	gnome_pilot_client_connect_to_daemon (gpc);

	if (pilot_arg!=NULL) {
		pilots = g_list_append (pilots, g_strdup (pilot_arg));
	} else {
		err = gnome_pilot_client_get_pilots (gpc, &pilots);
		if (err !=GPILOTD_OK || pilots == NULL) {
			g_warning (_("Unable to get PDA names"));
			show_warning_dialog (_("Unable to get PDA names"));
			exit (1);
		}
	}

	notfailed = failed = handles = NULL;

	survive = GNOME_Pilot_IMMEDIATE;
	if (later) survive = GNOME_Pilot_PERSISTENT;
	
	i=0;

	while (filenames && filenames[i]!=NULL) {
		gint err;
		err = gnome_pilot_client_install_file (gpc,
						       pilots->data, /* get first pilot */
						       filenames[i],
						       survive,
						       0,
						       &handle);
		if (err == GPILOTD_OK) {
			handles = g_slist_prepend (handles,GINT_TO_POINTER(handle));
			notfailed = g_slist_prepend (notfailed, (void *) filenames[i]);
		} else {
			failed = g_slist_prepend (failed, (void *) filenames[i]);
		}
		i++;
	}

	if (!later) {
		gchar *message;
		
		message = NULL;
		if (failed != NULL) {
			GSList *e;
			message = g_strdup (_("Following files failed :\n"));
			for (e=failed;e;e = g_slist_next (e)) {
				gchar *tmp;
				tmp = g_strconcat (message,"\t- ", e->data,"\n", NULL);
				g_free (message);
				message = tmp;
			}
			g_slist_free (failed);
		}
		{
			GSList *e;
			if (message == NULL)
				message = g_strdup_printf (_("Installing to %s:\n"), (char*)pilots->data);
			else {
				gchar *tmp;
				tmp = g_strconcat (message,"\nInstalling to ", 
						   (char*)pilots->data, ":\n", NULL);
				g_free (message);
				message = tmp;
			}
			for (e=notfailed;e;e = g_slist_next (e)) {
				gchar *tmp;
				tmp = g_strconcat (message,"\t- ", e->data,"\n", NULL);
				g_free (message);
				message = tmp;
			}
			g_slist_free (notfailed);
		}
		{
			gchar *tmp;
			gchar *info;

			if (handles == NULL) 
				info = g_strdup (_("No files to install"));
			else {
				
				info = g_strdup (_("Press synchronize on the cradle to install\n" 
						  " or cancel the operation."));
                                err = gnome_pilot_client_conduit (gpc,
                                                            pilots->data,
                                                            "File",
                                                            GNOME_Pilot_CONDUIT_DEFAULT,
                                                            survive,
                                                            0,
                                                            &handle);
			}
						
			tmp = g_strconcat (message==NULL?"":message,
					  "\n",
					  info,
					  NULL);
			g_free (message);
			g_free (info);
			message = tmp;
		}
		dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL,
		    GTK_MESSAGE_OTHER, GTK_BUTTONS_CANCEL, "%s",
		    message);
		gint response = GTK_RESPONSE_NONE;
		{
			GMainLoop *loop = g_main_loop_new (NULL, FALSE);
			g_signal_connect (dialog, "response",
			    G_CALLBACK (on_install_dialog_response), &response);
			g_signal_connect_swapped (dialog, "response",
			    G_CALLBACK (g_main_loop_quit), loop);
			gtk_window_present (GTK_WINDOW (dialog));
			g_main_loop_run (loop);
			g_main_loop_unref (loop);
		}
		if (dialog != NULL) /* if not destroyed by callback */
			gtk_window_destroy(GTK_WINDOW(dialog));
		if (response == GTK_RESPONSE_CANCEL) {
			GSList *e;
			for (e=handles;e;e = g_slist_next (e)) {
				gnome_pilot_client_remove_request
				    (gpc,GPOINTER_TO_INT(e->data));  
			}
			g_slist_free (handles);
		}
		g_free (message);
	}

	g_object_unref (G_OBJECT (gpc));

	return 0;
}

