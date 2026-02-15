/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-pilot-ddialog.c
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
#include "gnome-pilot-ddialog.h"


static GObjectClass *parent_class = NULL;

struct _GnomePilotDDialogPrivate 
{
	GtkBuilder *ui;

	GPilotDevice *device;

	GtkWidget *dialog;

	GtkWidget *device_name;
	GtkWidget *device_port;
	GtkWidget *device_port_combo;
	GtkWidget *device_port_label;
	GtkWidget *device_speed_combo;
	GtkWidget *device_speed_label;
	GtkWidget *device_timeout;
	GtkWidget *device_serial;
	GtkWidget *device_usb;
	GtkWidget *device_irda;
	GtkWidget *device_network;
	GtkWidget *device_bluetooth;
	GtkWidget *libusb_label;
	GList *libusb_list;
};

static void class_init (GnomePilotDDialogClass *klass);
static void init (GnomePilotDDialog *gpdd);

static gboolean get_widgets (GnomePilotDDialog *gpdd);
static void map_widgets (GnomePilotDDialog *gpdd);
static void init_widgets (GnomePilotDDialog *gpdd);
static void fill_widgets (GnomePilotDDialog *gpdd);

static void gpdd_dispose (GObject *object);
static void set_widget_visibility_by_type(GnomePilotDDialog *gpdd, int type);
static void network_device_toggled_callback (GtkCheckButton *btn,
    void *data);

GType
gnome_pilot_ddialog_get_type (void)
{
  static GType type = 0;

  if (type == 0)
    {
      static const GTypeInfo info =
      {
        sizeof (GnomePilotDDialogClass),
        NULL,
        NULL,
        (GClassInitFunc) class_init,
        NULL,
        NULL,
        sizeof (GnomePilotDDialog),
	0,
        (GInstanceInitFunc) init,
      };
      type = g_type_register_static (g_object_get_type (), "GnomePilotDDialog", &info, 0);
    }

  return type;
}

static void
class_init (GnomePilotDDialogClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek (g_object_get_type ());

	object_class->dispose = gpdd_dispose;
}

static void
init (GnomePilotDDialog *gpdd)
{
	GnomePilotDDialogPrivate *priv;
	guint error;
	GError *err = NULL;
	gchar *objects[] = {"DeviceSettings",
			    "timeout_adjustment",
			    "device_port_store",
			    NULL};
	
	priv = g_new0 (GnomePilotDDialogPrivate, 1);

	gpdd->priv = priv;

	/* Gui stuff */
	priv->ui = gtk_builder_new ();
	error = gtk_builder_add_objects_from_file (priv->ui, "gpilotd-capplet.ui", objects, NULL);
	if (error == 0) {
       	error = gtk_builder_add_objects_from_file (priv->ui, UIDATADIR "/gpilotd-capplet.ui", objects, &err);
		if (error == 0) {
			g_message ("gnome-pilot-ddialog init(): Could not load the GtkBuilder UI file: %s", err->message);
			goto error;
		}
	}

	if (!get_widgets (gpdd)) {
		g_message ("gnome-pilot-ddialog init(): Could not find all widgets in the UI file!");
		goto error;
	}

 error:
	;
}



GObject *
gnome_pilot_ddialog_new (GPilotDevice *device)
{
	GnomePilotDDialog *gpdd;
	GObject *object;
	
	object = G_OBJECT (g_type_create_instance (GNOME_PILOT_TYPE_DDIALOG));
	
	gpdd = GNOME_PILOT_DDIALOG (object);
	gpdd->priv->device = device;

	map_widgets (gpdd);
	fill_widgets (gpdd);
	init_widgets (gpdd);

	return object;
}

static gboolean
get_widgets (GnomePilotDDialog *gpdd)
{
	GnomePilotDDialogPrivate *priv;
	GtkTreeModel *model;
	GtkTreeIter iter;

	priv = gpdd->priv;

#define GW(name) GTK_WIDGET (gtk_builder_get_object (priv->ui, name))

	priv->dialog = GW ("DeviceSettings");

	priv->device_name = GW ("device_name_entry");
	priv->device_port_label = GW ("device_port_label");
	priv->device_port_combo = GW ("device_port_combo");
	priv->device_port = NULL;
	g_object_get (priv->device_port_combo, "child", &priv->device_port, NULL);
	priv->device_speed_combo = GW ("device_speed_combo");
	priv->device_speed_label = GW ("device_speed_label");
	priv->device_timeout = GW ("timeout_spinner");
	priv->device_serial = GW ("serial_radio");
	priv->device_usb = GW ("usb_radio");
	priv->device_irda = GW ("irda_radio");
	priv->device_network = GW ("network_radio");
	priv->device_bluetooth = GW ("bluetooth_radio");

	/* Doing the cell layout in glade .ui file seemed to result
	 * in duplicated text.  Probably a bug in glade/gtkbuilder?
	 */
	//	model = GTK_TREE_MODEL(gtk_list_store_new (1, G_TYPE_STRING));
	model = GTK_TREE_MODEL(gtk_builder_get_object (priv->ui,
		"device_port_store"));
	gtk_combo_box_set_model (GTK_COMBO_BOX (priv->device_port_combo), model);
	gtk_combo_box_set_entry_text_column (GTK_COMBO_BOX (priv->device_port_combo), 0);
	gtk_cell_layout_clear(GTK_CELL_LAYOUT(priv->device_port_combo));
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (priv->device_port_combo),
	    renderer, TRUE);
	gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (priv->device_port_combo),
	    renderer, "text", 0);
	/* usb: (libusb) pseudo-device is available from pilot-link 0.12.0 */
	gtk_list_store_prepend (GTK_LIST_STORE (model), &iter);
	gtk_list_store_set (GTK_LIST_STORE (model), &iter, 0, "usb:", -1);
	gtk_check_button_set_active(GTK_CHECK_BUTTON(priv->device_usb), 1);
	fill_speed_combo(GTK_COMBO_BOX(priv->device_speed_combo));

#undef GW
	return (priv->dialog
		&& priv->device_name
		&& priv->device_port
		&& priv->device_speed_combo
		&& priv->device_timeout
		&& priv->device_serial
		&& priv->device_usb
		&& priv->device_irda
		&& priv->device_network
		&& priv->device_bluetooth);
}

static void
map_widgets (GnomePilotDDialog *gpdd)
{
	GnomePilotDDialogPrivate *priv;
	
	priv = gpdd->priv;

	g_object_set_data (G_OBJECT (gpdd), "port_entry", priv->device_port);
	g_object_set_data (G_OBJECT (gpdd), "name_entry", priv->device_name);
	g_object_set_data (G_OBJECT (gpdd), "speed_combo", priv->device_speed_combo);
	g_object_set_data (G_OBJECT (gpdd), "irda_radio", priv->device_serial);
	g_object_set_data (G_OBJECT (gpdd), "usb_radio", priv->device_usb);
	g_object_set_data (G_OBJECT (gpdd), "irda_radio", priv->device_irda);
	g_object_set_data (G_OBJECT (gpdd), "network_radio", priv->device_network);
	g_object_set_data (G_OBJECT (gpdd), "bluetooth_radio", priv->device_bluetooth);
	g_object_set_data (G_OBJECT (gpdd), "timeout_spinner", priv->device_timeout);
}

static void 
init_widgets (GnomePilotDDialog *gpdd)
{
	GnomePilotDDialogPrivate *priv;

	priv = gpdd->priv;

	g_signal_connect (G_OBJECT (priv->device_port),"insert-text",
			    G_CALLBACK (insert_device_callback), NULL);
	g_signal_connect (G_OBJECT (priv->device_bluetooth), "toggled",
			    G_CALLBACK (network_device_toggled_callback), gpdd);
	g_signal_connect (G_OBJECT (priv->device_network), "toggled",
			    G_CALLBACK (network_device_toggled_callback), gpdd);
}

static void
fill_widgets (GnomePilotDDialog *gpdd)
{
	GnomePilotDDialogPrivate *priv;
	
	priv = gpdd->priv;

	if (priv->device) {
		gtk_editable_set_text (GTK_EDITABLE (priv->device_name), priv->device->name);
		if (priv->device->port != NULL)
			gtk_editable_set_text (GTK_EDITABLE (priv->device_port), priv->device->port);

		gtk_spin_button_set_value (GTK_SPIN_BUTTON (priv->device_timeout), priv->device->timeout);

		gtk_check_button_set_active (GTK_CHECK_BUTTON (priv->device_serial),
					      priv->device->type == PILOT_DEVICE_SERIAL);
		gtk_check_button_set_active (GTK_CHECK_BUTTON (priv->device_usb),
					      priv->device->type == PILOT_DEVICE_USB_VISOR);
		gtk_check_button_set_active (GTK_CHECK_BUTTON (priv->device_irda),
					      priv->device->type == PILOT_DEVICE_IRDA);
		gtk_check_button_set_active (GTK_CHECK_BUTTON (priv->device_network),
					      priv->device->type == PILOT_DEVICE_NETWORK);
		gtk_check_button_set_active (GTK_CHECK_BUTTON (priv->device_bluetooth),
					      priv->device->type == PILOT_DEVICE_BLUETOOTH);
		set_widget_visibility_by_type(gpdd, priv->device->type);
		speed_combo_set_speed(GTK_COMBO_BOX(priv->device_speed_combo),
				      priv->device->speed);
	}
}

static void
gpdd_run_response_cb (GtkDialog *dialog, gint response_id, gpointer user_data)
{
	GnomePilotDDialog *gpdd = GNOME_PILOT_DDIALOG (user_data);
	GnomePilotDDialogPrivate *priv = gpdd->priv;
	gboolean *done = g_object_get_data (G_OBJECT (dialog), "_done");
	gint *btn = g_object_get_data (G_OBJECT (dialog), "_btn");
	GPilotDevice **tmpdev_p = g_object_get_data (G_OBJECT (dialog), "_tmpdev");
	GPilotDevice *tmpdev = *tmpdev_p;

	if (response_id == GTK_RESPONSE_OK) {
		gchar *errstr = NULL;
		read_device_config (G_OBJECT (gpdd), tmpdev);
		if (check_device_settings (tmpdev, &errstr)) {
			*priv->device = *tmpdev;
			*btn = GTK_RESPONSE_OK;
			*done = TRUE;
		} else {
			error_dialog (NULL, errstr);
			g_free (errstr);
		}
	} else {
		*btn = response_id;
		*done = TRUE;
	}
}

gboolean
gnome_pilot_ddialog_run_and_close (GnomePilotDDialog *gpdd, GtkWindow *parent)
{
	GnomePilotDDialogPrivate *priv;
	gint btn = GTK_RESPONSE_CANCEL;
	GPilotDevice *tmpdev = g_new0 (GPilotDevice, 1);
	gboolean dialog_done = FALSE;

	priv = gpdd->priv;

	g_object_set_data (G_OBJECT (priv->dialog), "_done", &dialog_done);
	g_object_set_data (G_OBJECT (priv->dialog), "_btn", &btn);
	g_object_set_data (G_OBJECT (priv->dialog), "_tmpdev", &tmpdev);

	gtk_window_set_transient_for (GTK_WINDOW (priv->dialog), parent);
	g_signal_connect (priv->dialog, "response",
	    G_CALLBACK (gpdd_run_response_cb), gpdd);
	gtk_window_present (GTK_WINDOW (priv->dialog));

	while (!dialog_done)
		g_main_context_iteration (NULL, TRUE);

	g_signal_handlers_disconnect_by_func (priv->dialog,
	    G_CALLBACK (gpdd_run_response_cb), gpdd);

	g_free (tmpdev);
	gtk_widget_set_visible (priv->dialog, FALSE);

	return btn == GTK_RESPONSE_OK ? TRUE : FALSE;
}

static void
gpdd_dispose (GObject *object)
{
	GnomePilotDDialog *gpdd = GNOME_PILOT_DDIALOG (object);
	GnomePilotDDialogPrivate *priv;
	
	priv = gpdd->priv;

	gtk_window_destroy (GTK_WINDOW (priv->dialog));
	g_object_unref (G_OBJECT (priv->ui));
}

static void
network_device_toggled_callback (GtkCheckButton *btn, void *data)
{
	GnomePilotDDialog *gpdd = (GnomePilotDDialog *)data;
	GnomePilotDDialogPrivate *priv;
	int type;

	priv = gpdd->priv;

	/* toggled button could be bluetooth or network */
	if(btn == GTK_CHECK_BUTTON(priv->device_network) &&
	    gtk_check_button_get_active(GTK_CHECK_BUTTON(btn))) {
		type = PILOT_DEVICE_NETWORK;
	} else if (btn == GTK_CHECK_BUTTON(priv->device_bluetooth) &&
	    gtk_check_button_get_active(GTK_CHECK_BUTTON(btn))) {
		type = PILOT_DEVICE_BLUETOOTH;
	} else {
		type = PILOT_DEVICE_SERIAL;
	}
	set_widget_visibility_by_type(gpdd, type);
}

static void
set_widget_visibility_by_type(GnomePilotDDialog *gpdd, int type) {
	GnomePilotDDialogPrivate *priv;
	gboolean enable_extra_widgets = (type != PILOT_DEVICE_NETWORK &&
	    type != PILOT_DEVICE_BLUETOOTH);

	priv = gpdd->priv;

	gtk_widget_set_sensitive(priv->device_port_combo,
	    enable_extra_widgets);
	gtk_widget_set_sensitive(priv->device_port_label,
	    enable_extra_widgets);
	gtk_widget_set_sensitive(priv->device_speed_combo,
	    enable_extra_widgets);
	gtk_widget_set_sensitive(priv->device_speed_label,
	    enable_extra_widgets);
}
