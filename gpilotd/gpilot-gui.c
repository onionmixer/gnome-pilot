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
 *
 */

#include "gpilot-gui.h"
#include <string.h>
#include <stdlib.h>
#include <gtk/gtk.h>

static void gpilot_gui_run_dialog(GtkMessageType type, gchar *mesg, va_list ap);

static void
on_run_dialog_response (GtkDialog *dialog, gint response_id, gpointer user_data)
{
	GMainLoop *loop = (GMainLoop *) user_data;
	g_main_loop_quit (loop);
}

static void
gpilot_gui_run_dialog(GtkMessageType type, gchar *mesg, va_list ap)
{
	char *tmp;
	GtkWidget *dialog;
	GMainLoop *loop;

	tmp = g_strdup_vprintf(mesg,ap);

	dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, type, GTK_BUTTONS_OK, "%s", tmp);

	loop = g_main_loop_new (NULL, FALSE);
	g_signal_connect (dialog, "response", G_CALLBACK (on_run_dialog_response), loop);
	gtk_window_present (GTK_WINDOW (dialog));
	g_main_loop_run (loop);
	g_main_loop_unref (loop);

	gtk_window_destroy(GTK_WINDOW(dialog));
	g_free(tmp);
	va_end(ap);
}

void 
gpilot_gui_warning_dialog (gchar *mesg, ...) 
{
	va_list args;

	va_start (args, mesg);
	gpilot_gui_run_dialog (GTK_MESSAGE_WARNING, mesg, args);
	va_end (args);
}

void 
gpilot_gui_error_dialog (gchar *mesg, ...) 
{
	va_list args;

	va_start (args, mesg);
	gpilot_gui_run_dialog (GTK_MESSAGE_ERROR, mesg, args);
	va_end (args);
}

static void
on_restore_dialog_response (GtkDialog *dialog, gint response_id, gpointer user_data)
{
	gint *out_response = (gint *) user_data;
	*out_response = response_id;
}

GPilotPilot*
gpilot_gui_restore (GPilotContext *context,
		    GPilotPilot *pilot)
{
	GPilotPilot *result = NULL;
	GtkWidget *d;

	if (pilot) {
		gint response;
		char *tmp;
		GMainLoop *loop;
		tmp = g_strdup_printf ("Restore %s' pilot with id %d\n"
				       "and name `%s'",
				       pilot->pilot_username,
				       pilot->pilot_id,
				       pilot->name);
		d = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
		    GTK_BUTTONS_YES_NO, "%s", tmp);

		response = GTK_RESPONSE_NONE;
		loop = g_main_loop_new (NULL, FALSE);
		g_signal_connect (d, "response", G_CALLBACK (on_restore_dialog_response), &response);
		g_signal_connect_swapped (d, "response", G_CALLBACK (g_main_loop_quit), loop);
		gtk_window_present (GTK_WINDOW (d));
		g_main_loop_run (loop);
		g_main_loop_unref (loop);

		g_free (tmp);
		gtk_window_destroy(GTK_WINDOW(d));
		if (response == GTK_RESPONSE_YES) {
			result = pilot;
		} else {

		}
	} else {
		gpilot_gui_warning_dialog ("no ident\n"
					   "restoring pilot with ident\n"
					   "exciting things will soon be here...\n");
	}
	return result;
}
