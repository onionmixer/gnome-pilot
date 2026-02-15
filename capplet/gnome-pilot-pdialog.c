/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-pilot-pdialog.c
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

#include <sys/stat.h>
#include <pi-util.h>
#include "pilot.h"
#include "util.h"
#include "gnome-pilot-pdialog.h"


static GObjectClass *parent_class = NULL;

struct _GnomePilotPDialogPrivate 
{
	GtkBuilder *ui;

	GnomePilotClient *gpc;
	gint handle1, handle2;

	PilotState *state;
	GPilotPilot *pilot;

	GtkWidget *dialog;

	gchar *errstr;
	
	GtkWidget *pilot_username;
	GtkWidget *pilot_id;
	GtkWidget *pilot_get;
	GtkWidget *pilot_send;

	GtkWidget *pilot_name;
	GtkWidget *pilot_basedir;
	GtkWidget *pilot_charset;
	GtkWidget *pilot_charset_label;
	GtkWidget *pilot_charset_combo;

	GtkWidget *sync_dialog;
};

static void class_init (GnomePilotPDialogClass *klass);
static void init (GnomePilotPDialog *gppd);

static gboolean get_widgets (GnomePilotPDialog *gppd);
static void map_widgets (GnomePilotPDialog *gppd);
static void init_widgets (GnomePilotPDialog *gppd);
static void fill_widgets (GnomePilotPDialog *gppd);

static void gppd_pilot_get (GtkWidget *widget, gpointer user_data);
static void gppd_pilot_send (GtkWidget *widget, gpointer user_data);

static void gppd_request_completed (GnomePilotClient* client, 
				    const gchar *id, 
				    unsigned long handle, 
				    gpointer user_data);
static void gppd_userinfo_requested (GnomePilotClient *gpc, 
				     const gchar *device, 
				     const GNOME_Pilot_UserInfo *user, 
				     gpointer user_data);
static void gppd_system_info_requested (GnomePilotClient *gpc,
					const gchar *device,
					const GNOME_Pilot_SysInfo *sysinfo,
					gpointer user_data);

static void gppd_dispose (GObject *object);

GType
gnome_pilot_pdialog_get_type (void)
{
  static GType type = 0;

  if (type == 0)
    {
      static const GTypeInfo info =
      {
        sizeof (GnomePilotPDialogClass),
        NULL,
        NULL,
        (GClassInitFunc) class_init,
        NULL,
        NULL,
        sizeof (GnomePilotPDialog),
	0,
        (GInstanceInitFunc) init,
      };
      type = g_type_register_static (g_object_get_type (), "GnomePilotPDialog", &info, 0);
    }

  return type;
}

static void
class_init (GnomePilotPDialogClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek (g_object_get_type ());

	object_class->dispose = gppd_dispose;
}

static void
init (GnomePilotPDialog *gppd)
{
	GnomePilotPDialogPrivate *priv;
	guint error;
	gchar *objects[] = {"PilotSettings",
	                    "timeout_adjustment",
	                    "pilot_charset_store",
			    NULL};

	priv = g_new0 (GnomePilotPDialogPrivate, 1);

	gppd->priv = priv;

	/* Gui stuff */
	priv->ui = gtk_builder_new ();
	error = gtk_builder_add_objects_from_file (priv->ui, "gpilotd-capplet.ui", objects, NULL);
	if (error == 0) {
		error = gtk_builder_add_objects_from_file (priv->ui, UIDATADIR "/gpilotd-capplet.ui", objects, NULL);
		if (error == 0) {
			g_message ("gnome-pilot-pdialog init(): Could not load the GtkBuilder UI file!");
			goto error;
		}
	}

	if (!get_widgets (gppd)) {
		g_message ("gnome-pilot-pdialog init(): Could not find all widgets in the UI file!");
		goto error;
	}

 error:
	;
}



GObject *
gnome_pilot_pdialog_new (GnomePilotClient *gpc, PilotState *state, GPilotPilot *pilot)
{
	GnomePilotPDialog *gppd;
	GObject *object;
	
	object = G_OBJECT(g_type_create_instance (GNOME_PILOT_TYPE_PDIALOG));
	
	gppd = GNOME_PILOT_PDIALOG (object);
	gppd->priv->gpc = gpc;
	gppd->priv->state = state;
	gppd->priv->pilot = pilot;

	map_widgets (gppd);
	fill_widgets (gppd);
	init_widgets (gppd);

	gnome_pilot_client_connect__completed_request (gpc, gppd_request_completed, 
						       gppd);
	gnome_pilot_client_connect__user_info (gpc, gppd_userinfo_requested, 
					       gppd);
	gnome_pilot_client_connect__system_info (gpc, gppd_system_info_requested, 
						 gppd);
	
	return object;
}

void
gnome_pilot_pdialog_set_pilot (GObject *obj, GPilotPilot *pilot)
{
	GnomePilotPDialog *gppd = GNOME_PILOT_PDIALOG (obj);
	
	gppd->priv->pilot = pilot;
	fill_widgets (gppd);
}


static gboolean
get_widgets (GnomePilotPDialog *gppd)
{
	GnomePilotPDialogPrivate *priv;

	priv = gppd->priv;

#define GW(name) GTK_WIDGET (gtk_builder_get_object (priv->ui, name))

	priv->dialog = GW ("PilotSettings");

	priv->pilot_username = GW ("pilot_username_entry");
	priv->pilot_id = GW ("pilot_id_entry");
	priv->pilot_get = GW ("get_from_pilot_button");
	priv->pilot_send = GW ("send_to_pilot_button");
	
	priv->pilot_name = GW ("pilot_name_entry");
	priv->pilot_basedir = GW ("pilot_basedir_entry");
	priv->pilot_charset_label = GW ("pilot_charset_label");
	priv->pilot_charset_combo = GW ("pilot_charset_combo");
	priv->pilot_charset = NULL;
	g_object_get (priv->pilot_charset_combo, "child", &priv->pilot_charset, NULL);

	/* bug in gtkbuilder?  couldn't get the list store to work right with glade,
	 * so I do it here.
	 */
	GtkTreeModel *model = GTK_TREE_MODEL(gtk_builder_get_object (priv->ui,
		"pilot_charset_store"));
	gtk_combo_box_set_model (GTK_COMBO_BOX (priv->pilot_charset_combo), model);
	gtk_combo_box_set_entry_text_column (GTK_COMBO_BOX (priv->pilot_charset_combo), 0);
	gtk_cell_layout_clear(GTK_CELL_LAYOUT(priv->pilot_charset_combo));
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (priv->pilot_charset_combo),
	    renderer, TRUE);
	gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (priv->pilot_charset_combo),
	    renderer, "text", 0);

	
#undef GW
	return (priv->dialog
		&& priv->pilot_username
		&& priv->pilot_id
		&& priv->pilot_get
		&& priv->pilot_send
		&& priv->pilot_name
		&& priv->pilot_basedir
		&& priv->pilot_charset
		&& priv->pilot_charset_label
		&& priv->pilot_charset_combo);
}

static void
map_widgets (GnomePilotPDialog *gppd)
{
	GnomePilotPDialogPrivate *priv;
	
	priv = gppd->priv;

	g_object_set_data (G_OBJECT (gppd), "username", priv->pilot_username);
	g_object_set_data (G_OBJECT (gppd), "pilotid", priv->pilot_id);
	g_object_set_data (G_OBJECT (gppd), "pilotname", priv->pilot_name);
	g_object_set_data (G_OBJECT (gppd), "basedir", priv->pilot_basedir);
	g_object_set_data (G_OBJECT (gppd), "charset", priv->pilot_charset);
}

static void 
init_widgets (GnomePilotPDialog *gppd)
{
	GnomePilotPDialogPrivate *priv;

	priv = gppd->priv;

	/* Button signals */
	g_signal_connect (G_OBJECT (priv->pilot_get), "clicked",
			    G_CALLBACK (gppd_pilot_get), gppd);

	g_signal_connect (G_OBJECT (priv->pilot_send), "clicked",
			    G_CALLBACK (gppd_pilot_send), gppd);
	
	/* Other widget signals */
	g_signal_connect (G_OBJECT (priv->pilot_username),"insert-text",
			    G_CALLBACK (insert_username_callback), NULL);
	g_signal_connect (G_OBJECT (priv->pilot_id),"insert-text",
			    G_CALLBACK (insert_numeric_callback), NULL);
}

static void
fill_widgets (GnomePilotPDialog *gppd)
{
	GnomePilotPDialogPrivate *priv;
	char buf[256];
	
	priv = gppd->priv;

	if (priv->pilot) {
		gtk_editable_set_text (GTK_EDITABLE (priv->pilot_username), priv->pilot->pilot_username);

		g_snprintf (buf, sizeof (buf), "%d", priv->pilot->pilot_id);
		gtk_editable_set_text (GTK_EDITABLE (priv->pilot_id), buf);

		gtk_editable_set_text (GTK_EDITABLE (priv->pilot_name), priv->pilot->name);
		gtk_editable_set_text (GTK_EDITABLE (priv->pilot_basedir), priv->pilot->sync_options.basedir);
		gtk_editable_set_text (GTK_EDITABLE (priv->pilot_charset), priv->pilot->pilot_charset);
	}
}

static void
gppd_run_response_cb (GtkDialog *dialog, gint response_id, gpointer user_data)
{
	GnomePilotPDialog *gppd = GNOME_PILOT_PDIALOG (user_data);
	GnomePilotPDialogPrivate *priv = gppd->priv;
	gboolean *done = g_object_get_data (G_OBJECT (dialog), "_done");
	gint *btn = g_object_get_data (G_OBJECT (dialog), "_btn");

	if (response_id == GTK_RESPONSE_OK) {
		if (check_pilot_charset (gtk_editable_get_text (
			    GTK_EDITABLE (priv->pilot_charset)), &priv->errstr) &&
		    check_base_directory (gtk_editable_get_text (
			    GTK_EDITABLE (priv->pilot_basedir)), &priv->errstr)) {
			read_pilot_config (G_OBJECT (gppd), priv->pilot);
			*btn = GTK_RESPONSE_OK;
			*done = TRUE;
		} else {
			error_dialog (GTK_WINDOW (priv->dialog), priv->errstr);
			g_free (priv->errstr);
			priv->errstr = NULL;
			/* Don't close -- let user fix and try again */
		}
	} else {
		*btn = response_id;
		*done = TRUE;
	}
}

gboolean
gnome_pilot_pdialog_run_and_close (GnomePilotPDialog *gppd, GtkWindow *parent)
{
	GnomePilotPDialogPrivate *priv;
	gint btn = GTK_RESPONSE_CANCEL;
	gboolean dialog_done = FALSE;

	priv = gppd->priv;

	g_object_set_data (G_OBJECT (priv->dialog), "_done", &dialog_done);
	g_object_set_data (G_OBJECT (priv->dialog), "_btn", &btn);

	gtk_window_set_transient_for (GTK_WINDOW (priv->dialog), parent);

	g_signal_connect (priv->dialog, "response",
	    G_CALLBACK (gppd_run_response_cb), gppd);
	gtk_window_present (GTK_WINDOW (priv->dialog));

	while (!dialog_done)
		g_main_context_iteration (NULL, TRUE);

	g_signal_handlers_disconnect_by_func (priv->dialog,
	    G_CALLBACK (gppd_run_response_cb), gppd);

	gtk_widget_set_visible (priv->dialog, FALSE);

	return btn == GTK_RESPONSE_OK ? TRUE : FALSE;
}

static void 
gppd_request_completed (GnomePilotClient* client, 
			const gchar *id, 
			unsigned long handle, 
			gpointer user_data) 
{
	GnomePilotPDialog *gppd = GNOME_PILOT_PDIALOG (user_data);
	GnomePilotPDialogPrivate *priv;
	
	priv = gppd->priv;

	if (handle == priv->handle1)
		priv->handle1 = -1;
	else if (handle == priv->handle2)
		priv->handle2 = -1;
	else
		return;

	if (priv->handle1 == -1 && priv->handle2 == -1) {
		gtk_dialog_response (GTK_DIALOG (priv->sync_dialog), 
		    GTK_RESPONSE_OK);
	}
}

static void 
gppd_userinfo_requested (GnomePilotClient *gpc, 
			 const gchar *device, 
			 const GNOME_Pilot_UserInfo *user, 
			 gpointer user_data) 
{
	GnomePilotPDialog *gppd = GNOME_PILOT_PDIALOG (user_data);
	GnomePilotPDialogPrivate *priv;
	gchar buf[20];
	
	priv = gppd->priv;
	
	priv->pilot->pilot_id = user->userID;

	if (priv->pilot->pilot_username) 
		g_free (priv->pilot->pilot_username);
	priv->pilot->pilot_username = g_strdup (user->username);

	gtk_editable_set_text (GTK_EDITABLE (priv->pilot_username), priv->pilot->pilot_username);
	g_snprintf (buf, sizeof (buf), "%d", priv->pilot->pilot_id);
	gtk_editable_set_text (GTK_EDITABLE (priv->pilot_id), buf);
}

static void 
gppd_system_info_requested (GnomePilotClient *gpc,
			    const gchar *device,
			    const GNOME_Pilot_SysInfo *sysinfo,
			    gpointer user_data) 
{
	GnomePilotPDialog *gppd = GNOME_PILOT_PDIALOG (user_data);
	GnomePilotPDialogPrivate *priv;
	
	priv = gppd->priv;
	
	priv->pilot->creation = sysinfo->creation;
	priv->pilot->romversion = sysinfo->romVersion;
}


static void 
gppd_cancel_sync (GtkWidget *widget, gpointer user_data)
{
	GnomePilotPDialog *gppd = GNOME_PILOT_PDIALOG (user_data);
	GnomePilotPDialogPrivate *priv;
	
	priv = gppd->priv;

	gnome_pilot_client_remove_request (priv->gpc, priv->handle1);
	gnome_pilot_client_remove_request (priv->gpc, priv->handle2);

	priv->handle1 = -1;
	priv->handle2 = -1;
}

static void
gppd_sync_dialog_response_cb (GtkDialog *dialog, gint response_id, gpointer user_data)
{
	GnomePilotPDialog *gppd = GNOME_PILOT_PDIALOG (user_data);
	gboolean *done = g_object_get_data (G_OBJECT (dialog), "_done");

	if (GTK_RESPONSE_CANCEL == response_id) {
		gppd_cancel_sync (GTK_WIDGET (dialog), gppd);
	}
	*done = TRUE;
}

static void
gppd_sync_dialog (GnomePilotPDialog *gppd,
		  GPilotDevice* device)
{
	GnomePilotPDialogPrivate *priv;
	gchar *location;
	gboolean dialog_done = FALSE;

	priv = gppd->priv;

	location = device->type == PILOT_DEVICE_NETWORK ? "netsync" : device->port;
	priv->sync_dialog = gtk_message_dialog_new (GTK_WINDOW(priv->dialog),
                                                   GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_INFO,
                                                   GTK_BUTTONS_CANCEL,
                                                   _("Please put PDA in %s (%s) and press "
                                                     "HotSync button or cancel the operation."),
	    					   device->name, location);

	g_object_set_data (G_OBJECT (priv->sync_dialog), "_done", &dialog_done);
	g_signal_connect (priv->sync_dialog, "response",
	    G_CALLBACK (gppd_sync_dialog_response_cb), gppd);
	gtk_window_present (GTK_WINDOW (priv->sync_dialog));
	while (!dialog_done)
		g_main_context_iteration (NULL, TRUE);
	gtk_window_destroy (GTK_WINDOW (priv->sync_dialog));
	priv->sync_dialog = NULL;
}

static void 
gppd_pilot_get (GtkWidget *widget, gpointer user_data)
{
	GnomePilotPDialog *gppd = GNOME_PILOT_PDIALOG (user_data);
	GnomePilotPDialogPrivate *priv;
	GPilotDevice *dev;
	
	priv = gppd->priv;

	dev = choose_pilot_dialog (priv->state);
	if (dev != NULL) {
 		if (gnome_pilot_client_get_user_info (priv->gpc, 
						      dev->name, 
						      GNOME_Pilot_IMMEDIATE, 
						      0, 
						      &priv->handle1)== GPILOTD_OK &&
		    gnome_pilot_client_get_system_info (priv->gpc,
							dev->name,
							GNOME_Pilot_IMMEDIATE,
							0,
							&priv->handle2) == GPILOTD_OK) {
			gppd_sync_dialog (gppd, dev);
		} else {
			error_dialog (GTK_WINDOW (priv->dialog), _("The request to get PDA ID failed"));
		}
	}
}

static void 
gppd_pilot_send (GtkWidget *widget, gpointer user_data)
{
	GnomePilotPDialog *gppd = GNOME_PILOT_PDIALOG (user_data);
	GnomePilotPDialogPrivate *priv;
	GNOME_Pilot_UserInfo user;
	GPilotPilot *pilot;
	GPilotDevice *dev;

	priv = gppd->priv;

	dev = choose_pilot_dialog (priv->state);
	if (dev != NULL){
		pilot = g_new0 (GPilotPilot, 1);
		
		read_pilot_config (G_OBJECT (gppd), pilot);

		user.userID = pilot->pilot_id;
		user.username = g_strdup (pilot->pilot_username);

		if (gnome_pilot_client_set_user_info (priv->gpc, 
						      dev->name, 
						      user, 
						      FALSE, 
						      GNOME_Pilot_IMMEDIATE, 
						      0, 
						      &priv->handle1)== GPILOTD_OK &&
		    gnome_pilot_client_get_system_info (priv->gpc,
							dev->name,
							GNOME_Pilot_IMMEDIATE,
							0,
							&priv->handle2) == GPILOTD_OK) {
			gppd_sync_dialog (gppd, dev);
		} else {
			error_dialog (GTK_WINDOW (priv->dialog), _("The request to set PDA ID failed"));
		}
		gpilot_pilot_free (pilot);	
	}
}

static void
gppd_dispose (GObject *object)
{
	GnomePilotPDialog *gppd = GNOME_PILOT_PDIALOG (object);
	GnomePilotPDialogPrivate *priv;
	
	priv = gppd->priv;

	gtk_window_destroy (GTK_WINDOW (priv->dialog));
	g_object_unref (G_OBJECT (priv->ui));

	g_signal_handlers_disconnect_matched (priv->gpc,
	    G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, object);

}
