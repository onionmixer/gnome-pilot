/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-pilot-cdialog.c
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
#include "pilot.h"
#include "util.h"
#include "gnome-pilot-cdialog.h"


static GObjectClass *parent_class = NULL;

struct _GnomePilotCDialogPrivate 
{
	GtkBuilder *ui;

	ConduitState *state;

	GtkWidget *dialog;

	GtkWidget *settings_frame;
	GtkWidget *sync_actions;
	GtkWidget *sync_one_actions;
	
	GtkWidget *options_frame;;
};

static void class_init (GnomePilotCDialogClass *klass);
static void init (GnomePilotCDialog *gpcd);

static gboolean get_widgets (GnomePilotCDialog *gpcd);
static void init_widgets (GnomePilotCDialog *gpcd);
static void fill_widgets (GnomePilotCDialog *gpcd);

static void gpcd_action_activated (GtkWidget *widget, gpointer user_data);

static void gpcd_dispose (GObject *object);

GType
gnome_pilot_cdialog_get_type (void)
{
  static GType type = 0;

  if (type == 0)
    {
      static const GTypeInfo info =
      {
        sizeof (GnomePilotCDialogClass),
        NULL,
        NULL,
        (GClassInitFunc) class_init,
        NULL,
        NULL,
        sizeof (GnomePilotCDialog),
	0,
        (GInstanceInitFunc) init,
      };

      type = g_type_register_static (g_object_get_type (), "GnomePilotCDialog", &info, 0);
    }

  return type;
}

static void
class_init (GnomePilotCDialogClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek (g_object_get_type ());

	object_class->dispose = gpcd_dispose;
}

static void
init (GnomePilotCDialog *gpcd)
{
	GnomePilotCDialogPrivate *priv;
	guint error;
	gchar *objects[] = {"ConduitSettings",
			    NULL};

	priv = g_new0 (GnomePilotCDialogPrivate, 1);

	gpcd->priv = priv;

	/* Gui stuff */ 
	priv->ui = gtk_builder_new ();
	error = gtk_builder_add_objects_from_file (priv->ui, "gpilotd-capplet.ui", objects, NULL);
	if (error == 0) {
		error = gtk_builder_add_objects_from_file (priv->ui, UIDATADIR "/gpilotd-capplet.ui", objects, NULL);
		if (error == 0) {
			g_message ("gnome-pilot-cdialog init(): Could not load the GtkBuilder UI file!");
			goto error;
		}
	}

	if (!get_widgets (gpcd)) {
		g_message ("gnome-pilot-cdialog init(): Could not find all widgets in the UI file!");
		goto error;
	}
	
 error:
	;
}



GObject *
gnome_pilot_cdialog_new (ConduitState *state)
{
	GnomePilotCDialog *gpcd;
	GObject *object;
	
	object = G_OBJECT(g_type_create_instance(GNOME_PILOT_TYPE_CDIALOG));
	
	gpcd = GNOME_PILOT_CDIALOG (object);
	gpcd->priv->state = state;

	fill_widgets (gpcd);
	init_widgets (gpcd);
	
	return object;
}

static gboolean
get_widgets (GnomePilotCDialog *gpcd)
{
	GnomePilotCDialogPrivate *priv;

	priv = gpcd->priv;

#define GW(name) GTK_WIDGET (gtk_builder_get_object (priv->ui, name))

	priv->dialog = GW ("ConduitSettings");

	priv->settings_frame = GW ("settings_frame");
	priv->sync_actions = GW ("sync_actions_combo");
	priv->sync_one_actions = GW ("sync_one_actions_combo");
	
	priv->options_frame = GW ("options_frame");

#undef GW
	return (priv->dialog
		&& priv->settings_frame
		&& priv->sync_actions
		&& priv->sync_one_actions
		&& priv->options_frame);
}

static void 
init_widgets (GnomePilotCDialog *gpcd)
{
	GnomePilotCDialogPrivate *priv;
	
	priv = gpcd->priv;

	g_signal_connect (G_OBJECT (priv->sync_actions), "changed",
	    G_CALLBACK (gpcd_action_activated), gpcd);
}

static void
fill_widgets (GnomePilotCDialog *gpcd)
{
	GnomePilotCDialogPrivate *priv;
	
	priv = gpcd->priv;

	if (priv->state) {
		fill_conduit_sync_type_combo (GTK_COMBO_BOX (priv->sync_actions), priv->state);
		fill_conduit_first_sync_type_combo (GTK_COMBO_BOX (priv->sync_one_actions), priv->state);

		if (!priv->state->has_settings) {
			gtk_widget_set_visible (priv->settings_frame, FALSE);

		} else if (gnome_pilot_conduit_create_settings_window (priv->state->conduit, priv->options_frame) == 500) { /* < 0) { */
			gchar *msg = _("Unable to create PDA settings window. Incorrect conduit configuration.");
			error_dialog (GTK_WINDOW (priv->dialog), msg);

			/* Self healing. Will not try again for this run of the capplet */
			gnome_pilot_conduit_management_destroy_conduit (priv->state->management, &priv->state->conduit);
			priv->state->settings_widget = NULL;
			priv->state->has_settings = FALSE;
			priv->state->conduit = NULL;
			gtk_widget_set_visible (priv->settings_frame, FALSE);
		}
	}
}

GnomePilotConduitSyncType 
gnome_pilot_cdialog_sync_type (GnomePilotCDialog *gpcd)
{
	GnomePilotCDialogPrivate *priv;
	GtkTreeIter iter;
	GtkTreeModel *model;
	int sync_type = -1;
	
	priv = gpcd->priv;
	
	model = gtk_combo_box_get_model (GTK_COMBO_BOX(priv->sync_actions));
	if (gtk_combo_box_get_active_iter(GTK_COMBO_BOX(priv->sync_actions), &iter)) {
		gtk_tree_model_get(model, &iter, 1, &sync_type, -1);
	}

	return sync_type;
}

GnomePilotConduitSyncType 
gnome_pilot_cdialog_first_sync_type (GnomePilotCDialog *gpcd)
{
	GnomePilotCDialogPrivate *priv;
	GtkTreeIter iter;
	GtkTreeModel *model;
	int sync_type = -1;
	
	priv = gpcd->priv;
	
	model = gtk_combo_box_get_model (GTK_COMBO_BOX(priv->sync_one_actions));
	if (gtk_combo_box_get_active_iter(GTK_COMBO_BOX(priv->sync_one_actions), &iter)) {
		gtk_tree_model_get(model, &iter, 1, &sync_type, -1);
	}

	return sync_type;
}

static void
gpcd_run_response_cb (GtkDialog *dialog, gint response_id, gpointer user_data)
{
	gboolean *done = g_object_get_data (G_OBJECT (dialog), "_done");
	gint *btn = g_object_get_data (G_OBJECT (dialog), "_btn");
	*btn = response_id;
	*done = TRUE;
}

gboolean
gnome_pilot_cdialog_run_and_close (GnomePilotCDialog *gpcd, GtkWindow *parent)
{
	GnomePilotCDialogPrivate *priv;
	gint btn = GTK_RESPONSE_CANCEL;
	gboolean dialog_done = FALSE;

	priv = gpcd->priv;

	g_object_set_data (G_OBJECT (priv->dialog), "_done", &dialog_done);
	g_object_set_data (G_OBJECT (priv->dialog), "_btn", &btn);

	gtk_window_set_transient_for (GTK_WINDOW (priv->dialog), parent);
	fill_conduit_sync_type_combo (GTK_COMBO_BOX (priv->sync_actions), priv->state);
	g_signal_connect (priv->dialog, "response",
	    G_CALLBACK (gpcd_run_response_cb), gpcd);
	gtk_window_present (GTK_WINDOW (priv->dialog));

	while (!dialog_done)
		g_main_context_iteration (NULL, TRUE);

	g_signal_handlers_disconnect_by_func (priv->dialog,
	    G_CALLBACK (gpcd_run_response_cb), gpcd);

	gtk_widget_set_visible (priv->dialog, FALSE);

	return GTK_RESPONSE_OK == btn ? TRUE : FALSE;
}

static void 
gpcd_action_activated (GtkWidget *widget, gpointer user_data)
{
	GnomePilotCDialog *gpcd = GNOME_PILOT_CDIALOG (user_data);
	GnomePilotCDialogPrivate *priv;
	gboolean disable;
	
	priv = gpcd->priv;
	
	disable = (gnome_pilot_cdialog_sync_type (gpcd) == GnomePilotConduitSyncTypeNotSet);
	
	gtk_widget_set_sensitive (priv->sync_one_actions, !disable);
	gtk_widget_set_sensitive (priv->options_frame, !disable);
}

static void
gpcd_dispose (GObject *object)
{
	GnomePilotCDialog *gpcd = GNOME_PILOT_CDIALOG (object);
	GnomePilotCDialogPrivate *priv;
	
	priv = gpcd->priv;

	g_object_unref (G_OBJECT (priv->ui));

}
