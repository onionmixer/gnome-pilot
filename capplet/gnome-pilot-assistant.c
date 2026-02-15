/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-pilot-assistant.c
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "pilot.h"
#include "util.h"
#include "gnome-pilot-assistant.h"

#define GPD_PAGE_WELCOME 0
#define GPD_PAGE_CRADLE 1
#define GPD_PAGE_ERROR 2
#define GPD_PAGE_PILOT_ONE 3
#define GPD_PAGE_SYNC 4
#define GPD_PAGE_PILOT_TWO 5
#define GPD_PAGE_FINISH 6

static GObjectClass *parent_class = NULL;

struct _GnomePilotAssistantPrivate 
{
	GtkBuilder *ui;

	GnomePilotClient *gpc;
	gint handle1;
	gint handle2;

	gboolean finished;
	gboolean started;
	gchar *errstr;
	GMainLoop *loop;
	
	PilotState *state;
	PilotState *orig_state;
	GPilotDevice *device;
	GPilotPilot *pilot;

	GtkWidget *assistant;

	GtkWidget *page_cradle;
	GtkWidget *page_error;
	GtkWidget *page_pilot_one;
	GtkWidget *page_sync;
	GtkWidget *page_pilot_two;
	GtkWidget *page_finish;

	GtkWidget *device_name;
	GtkWidget *device_port;
	GtkWidget *device_port_combo;
	GtkWidget *device_port_label;
	GtkWidget *device_speed;
	GtkWidget *device_speed_label;
	GtkWidget *device_timeout;
	GtkWidget *device_usb;
	GtkWidget *device_irda;
	GtkWidget *device_network;
	GtkWidget *device_bluetooth;
	
	GtkWidget *pilot_info;
	GtkWidget *pilot_info_no;
	GtkWidget *pilot_username;
	GtkWidget *pilot_id;

	GtkWidget *sync_label_vbox;
	GtkWidget *sync_label;

	GtkWidget *pilot_name;
	GtkWidget *pilot_basedir;
	GtkWidget *pilot_charset;
	GtkWidget *pilot_charset_label;
	GtkWidget *pilot_charset_combo;
};

static void class_init (GnomePilotAssistantClass *klass);
static void init (GnomePilotAssistant *gpd);

static gboolean get_widgets (GnomePilotAssistant *gpd);
static void map_widgets (GnomePilotAssistant *gpd);
static void init_widgets (GnomePilotAssistant *gpd);
static void fill_widgets (GnomePilotAssistant *gpd);
static void set_widget_visibility_by_type(GnomePilotAssistant *gpd, int type);
static void network_device_toggled_callback (GtkCheckButton *btn,
    void *data);

static gboolean gpd_delete_window (GtkWindow *window, gpointer user_data);
static void gpd_canceled (GtkAssistant *assistant, gpointer user_data);

static gint gpd_forward_page(gint current_page, gpointer user_data);
static void gpd_page_prepare (GtkAssistant *assistant, GtkWidget *page, gpointer user_data);
static gboolean gpd_cancel_sync (GnomePilotAssistant *gpd);
static void gpd_finish_page_finished (GtkAssistant *assistant, gpointer user_data);

static gboolean gpd_cradle_page_next (GnomePilotAssistant *gpd);

static void gpd_sync_page_prepare (GnomePilotAssistant *gpd);
static gboolean gpd_pilot_page_two_next (GnomePilotAssistant *gpd);

static void gpd_device_info_check (GtkEditable *editable, gpointer user_data);
static void gpd_pilot_name_check (GtkEditable *editable, gpointer user_data);
static void gpd_pilot_info_check (GtkEditable *editable, gpointer user_data);
static void gpd_pilot_info_button (GtkToggleButton *toggle, gpointer user_data);

static void gpd_request_completed (GnomePilotClient* client, const gchar *id, gint handle, gpointer user_data);
static void gpd_userinfo_requested (GnomePilotClient *gpc, const gchar *device, const GNOME_Pilot_UserInfo *user, gpointer user_data);
static void gpd_system_info_requested (GnomePilotClient *gpc,
 const gchar *device, const GNOME_Pilot_SysInfo *sysinfo, gpointer user_data);

static void gpd_dispose (GObject *object);

GType
gnome_pilot_assistant_get_type (void)
{
  static GType type = 0;

  if (type == 0)
    {
      static const GTypeInfo info =
      {
        sizeof (GnomePilotAssistantClass),
        NULL,
        NULL,
        (GClassInitFunc) class_init,
        NULL,
        NULL,
        sizeof (GnomePilotAssistant),
	0,
        (GInstanceInitFunc) init,
      };
      
      type = g_type_register_static (g_object_get_type (), "GnomePilotAssistant", &info, 0);
    }

  return type;
}

static void
class_init (GnomePilotAssistantClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek (g_object_get_type ());

	object_class->dispose = gpd_dispose;
}

static void
init (GnomePilotAssistant *gpd)
{
	GnomePilotAssistantPrivate *priv;
	guint error;
	gchar *objects[] = {"Assistant",
	                    "assistant_device_timeout_adjustment",
	                    "assistant_pilot_charset_store",
	                    "assistant_device_port_store",
	                    "assistant_device_speed_store", NULL};    

	priv = g_new0 (GnomePilotAssistantPrivate, 1);

	gpd->priv = priv;

	priv->finished = FALSE;
	priv->started = FALSE;
	priv->errstr = NULL;
	
	/* State information */
	loadPilotState (&priv->orig_state);
	priv->state = dupPilotState (priv->orig_state);
	priv->pilot = g_new0 (GPilotPilot, 1);
	priv->device = g_new0 (GPilotDevice,1);

	/* Gui stuff */
	priv->ui = gtk_builder_new ();
	error = gtk_builder_add_objects_from_file (priv->ui, "gpilotd-capplet.ui", objects, NULL);
	if (error == 0) {
		error = gtk_builder_add_objects_from_file (priv->ui, UIDATADIR "/gpilotd-capplet.ui", objects, NULL);
		if (error == 0) {
			g_message ("gnome-pilot-assistant init(): Could not load the GtkBuilder UI file!");
			goto error;
		}
	}

	if (!get_widgets (gpd)) {
		g_message ("gnome-pilot-assistant init(): Could not find all widgets in the UI file!");
		goto error;
	}

	map_widgets (gpd);
	fill_widgets (gpd);
	init_widgets (gpd);

 error:
	;
}



GObject *
gnome_pilot_assistant_new (GnomePilotClient *gpc)
{
	GnomePilotAssistant *gpd;
	GObject *obj;
	
	obj = G_OBJECT(g_type_create_instance (GNOME_PILOT_TYPE_ASSISTANT));
	
	gpd = GNOME_PILOT_ASSISTANT (obj);
	gpd->priv->gpc = gpc;

	g_signal_connect   (G_OBJECT (gpc), "completed_request",
			    G_CALLBACK (gpd_request_completed), gpd);
	g_signal_connect   (G_OBJECT (gpc), "user_info",
			    G_CALLBACK (gpd_userinfo_requested), gpd);
	g_signal_connect   (G_OBJECT (gpc), "system_info",
			    G_CALLBACK (gpd_system_info_requested), gpd);

	return obj;
}

static gboolean
get_widgets (GnomePilotAssistant *gpd)
{
	GnomePilotAssistantPrivate *priv;
	GtkTreeModel *model;
	GtkTreeIter iter;    

	GtkWidget *w;

	priv = gpd->priv;

//#define GW(name) GTK_WIDGET (gtk_builder_get_object (priv->ui, name))
#define GW(name) w = GTK_WIDGET (gtk_builder_get_object (priv->ui, name)); if (!w) printf("'%s'\n", name)

	priv->assistant = GW ("Assistant");

	priv->page_cradle = GW ("page_cradle");
	priv->page_error = GW ("page_error");
	priv->page_pilot_one = GW ("page_pilot1");
	priv->page_sync = GW ("page_sync");
	priv->page_pilot_two = GW ("page_pilot2");
	priv->page_finish = GW ("page_finish");


	gtk_assistant_set_page_complete (GTK_ASSISTANT (priv->assistant),
	                                 gtk_assistant_get_nth_page (GTK_ASSISTANT (priv->assistant), GPD_PAGE_WELCOME),
	                                 TRUE);
	gtk_assistant_set_page_complete (GTK_ASSISTANT (priv->assistant),
	    priv->page_cradle, FALSE);
	gtk_assistant_set_page_complete (GTK_ASSISTANT (priv->assistant),
	    priv->page_error, FALSE);
	gtk_assistant_set_page_complete (GTK_ASSISTANT (priv->assistant),
	    priv->page_pilot_one, TRUE);
	gtk_assistant_set_page_complete (GTK_ASSISTANT (priv->assistant),
	    priv->page_pilot_two, FALSE);

	priv->device_name = GW ("assistant_device_name_entry");
	priv->device_port_combo = GW ("assistant_device_port_combo");
	priv->device_port = NULL;
	g_object_get (priv->device_port_combo, "child", &priv->device_port, NULL);
	priv->device_port_label = GW ("assistant_device_port_label");
	priv->device_speed = GW ("assistant_device_speed_combo");
	priv->device_speed_label = GW ("assistant_device_speed_label");
	priv->device_timeout = GW ("assistant_device_timeout_spinner");
	priv->device_usb = GW ("assistant_usb_radio");
	priv->device_irda = GW ("assistant_irda_radio");
	priv->device_network = GW ("assistant_network_radio");
	priv->device_bluetooth = GW ("assistant_bluetooth_radio");

	/* Doing the cell layout in glade .ui file seemed to result
	 * in duplicated text.  Probably a bug in glade/gtkbuilder?
	 */
	//	model = GTK_TREE_MODEL(gtk_list_store_new (1, G_TYPE_STRING));
	model = GTK_TREE_MODEL(gtk_builder_get_object (priv->ui,
		"assistant_device_port_store"));
	gtk_combo_box_set_model (GTK_COMBO_BOX (priv->device_port_combo), model);
	gtk_combo_box_set_entry_text_column (GTK_COMBO_BOX (
		priv->device_port_combo), 0);
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


	gtk_combo_box_set_active(
	    GTK_COMBO_BOX (priv->device_port_combo), 0);

	priv->pilot_info = GW ("pilot_user_frame");
	priv->pilot_info_no = GW ("no_radio_button");
	priv->pilot_username = GW ("assistant_pilot_username_entry");
	priv->pilot_id = GW ("assistant_pilot_id_entry");
	
	priv->sync_label_vbox = GW ("page_sync");
	priv->sync_label =  gtk_label_new ("");
	gtk_widget_set_vexpand (priv->sync_label, TRUE);
	gtk_widget_set_margin_top (priv->sync_label, 4);
	gtk_widget_set_margin_bottom (priv->sync_label, 4);
	gtk_box_append (GTK_BOX (priv->sync_label_vbox), priv->sync_label);

	priv->pilot_name = GW ("assistant_pilot_name_entry");
	priv->pilot_basedir = GW ("assistant_pilot_basedir_entry");
	priv->pilot_charset_label = GW ("assistant_pilot_charset_label");
	priv->pilot_charset_combo = GW ("assistant_pilot_charset_combo");
	priv->pilot_charset = NULL;
	g_object_get (priv->pilot_charset_combo, "child", &priv->pilot_charset, NULL);


#undef GW

	return (priv->assistant
		&& priv->device_name
		&& priv->device_port
		&& priv->device_speed
		&& priv->device_timeout
		&& priv->device_usb
		&& priv->device_irda
		&& priv->device_network
		&& priv->device_bluetooth
		&& priv->pilot_info
		&& priv->pilot_info_no
		&& priv->pilot_username
		&& priv->pilot_id
		&& priv->sync_label_vbox
		&& priv->sync_label
		&& priv->pilot_name
		&& priv->pilot_basedir
		&& priv->pilot_charset
		&& priv->pilot_charset_label
		&& priv->pilot_charset_combo);
}

static void
map_widgets (GnomePilotAssistant *gpd)
{
	GnomePilotAssistantPrivate *priv;
	
	priv = gpd->priv;
	
	g_object_set_data (G_OBJECT (gpd), "port_entry", priv->device_port);
	g_object_set_data (G_OBJECT (gpd), "name_entry", priv->device_name);
	g_object_set_data (G_OBJECT (gpd), "speed_combo", priv->device_speed);
	g_object_set_data (G_OBJECT (gpd), "timeout_spinner", priv->device_timeout);
	g_object_set_data (G_OBJECT (gpd), "usb_radio", priv->device_usb);
	g_object_set_data (G_OBJECT (gpd), "irda_radio", priv->device_irda);
	g_object_set_data (G_OBJECT (gpd), "network_radio", priv->device_network);
	g_object_set_data (G_OBJECT (gpd), "bluetooth_radio", priv->device_bluetooth);

	g_object_set_data (G_OBJECT (gpd), "username", priv->pilot_username);
	g_object_set_data (G_OBJECT (gpd), "pilotid", priv->pilot_id);
	g_object_set_data (G_OBJECT (gpd), "pilotname", priv->pilot_name);
	g_object_set_data (G_OBJECT (gpd), "basedir", priv->pilot_basedir);
	g_object_set_data (G_OBJECT (gpd), "charset", priv->pilot_charset);
}

static void 
init_widgets (GnomePilotAssistant *gpd)
{
	GnomePilotAssistantPrivate *priv;

	priv = gpd->priv;

	/* Main signals */
	g_signal_connect   (G_OBJECT (priv->assistant), "close-request",
	    G_CALLBACK (gpd_delete_window), gpd);

	g_signal_connect   (G_OBJECT (priv->assistant), "cancel",
	    G_CALLBACK (gpd_canceled), gpd);
	//	g_signal_connect   (G_OBJECT (priv->assistant), "help",
	//   G_CALLBACK (gpd_help), gpd);


	/* Page signals */
	gtk_assistant_set_forward_page_func(GTK_ASSISTANT(priv->assistant),
	    gpd_forward_page, gpd, NULL);
	g_signal_connect_after (G_OBJECT (priv->assistant), "prepare",
	    G_CALLBACK (gpd_page_prepare), gpd);


	/* Other widget signals */
	g_signal_connect   (G_OBJECT (priv->device_name),"changed",
			    G_CALLBACK (gpd_device_info_check), gpd);
	g_signal_connect   (G_OBJECT (priv->device_port),"insert-text",
			    G_CALLBACK (insert_device_callback), NULL);
	g_signal_connect   (G_OBJECT (priv->device_network), "toggled",
			    G_CALLBACK (network_device_toggled_callback), gpd);
	g_signal_connect   (G_OBJECT (priv->device_bluetooth), "toggled",
			    G_CALLBACK (network_device_toggled_callback), gpd);
	g_signal_connect   (G_OBJECT (priv->device_port),"changed",
			    G_CALLBACK (gpd_device_info_check), gpd);

	g_signal_connect   (G_OBJECT (priv->pilot_info_no),"toggled",
			    G_CALLBACK (gpd_pilot_info_button), gpd);
	g_signal_connect   (G_OBJECT (priv->pilot_username),"insert-text",
			    G_CALLBACK (insert_username_callback), NULL);
	g_signal_connect   (G_OBJECT (priv->pilot_username),"changed",
			    G_CALLBACK (gpd_pilot_info_check), gpd);
	g_signal_connect   (G_OBJECT (priv->pilot_id),"insert-text",
			    G_CALLBACK (insert_numeric_callback), NULL);
	g_signal_connect   (G_OBJECT (priv->pilot_id),"changed",
			    G_CALLBACK (gpd_pilot_info_check), gpd);
	g_signal_connect   (G_OBJECT (priv->pilot_name),"changed",
			    G_CALLBACK (gpd_pilot_name_check), gpd);

}

static void
fill_widgets (GnomePilotAssistant *gpd)
{
	GnomePilotAssistantPrivate *priv;
	gchar buf[256];
	char *str, *str2;
	
	priv = gpd->priv;
	
	/* Cradle page */
	str = next_cradle_name (priv->state);
	gtk_editable_set_text (GTK_EDITABLE (priv->device_name), str);
	g_free (str);
	set_widget_visibility_by_type(gpd,
	    (gtk_check_button_get_active(GTK_CHECK_BUTTON(priv->device_network)) ||
		gtk_check_button_get_active(GTK_CHECK_BUTTON(priv->device_bluetooth))) ?
	    PILOT_DEVICE_NETWORK : PILOT_DEVICE_SERIAL);

	/* First pilot page */
	gtk_editable_set_text (GTK_EDITABLE (priv->pilot_username), g_get_real_name ());

	g_snprintf (buf, sizeof (buf), "%d", getuid ());
	gtk_editable_set_text (GTK_EDITABLE (priv->pilot_id), buf);

	/* Second pilot page */
	str = next_pilot_name (priv->state);
	gtk_editable_set_text (GTK_EDITABLE (priv->pilot_name), str);

	str2 = g_build_filename (g_get_home_dir (), str, NULL);
	gtk_editable_set_text (GTK_EDITABLE (priv->pilot_basedir), str2);
	gtk_editable_set_text (GTK_EDITABLE (priv->pilot_charset),
	    get_default_pilot_charset());

	g_free (str);
	g_free (str2);
}

gboolean
gnome_pilot_assistant_run_and_close (GnomePilotAssistant *gpd)
{
	GnomePilotAssistantPrivate *priv;
	gboolean result;
	
	priv = gpd->priv;
	
	gtk_window_present (GTK_WINDOW (priv->assistant));

	priv->loop = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (priv->loop);
	g_main_loop_unref (priv->loop);
	priv->loop = NULL;

	result = priv->finished;
	
	g_object_unref (G_OBJECT (gpd));

	return result;
}

static void
cancel_dialog_response_cb (GtkDialog *dlg, gint response_id, gpointer user_data)
{
	gboolean *done = g_object_get_data (G_OBJECT (dlg), "_done");
	gint *resp = g_object_get_data (G_OBJECT (dlg), "_response");
	*resp = response_id;
	*done = TRUE;
}

static gboolean
cancel_dialog (GnomePilotAssistant *gpd)
{
	GnomePilotAssistantPrivate *priv;
	GtkWidget *dlg;
	gboolean dialog_done = FALSE;
	gint dialog_response = GTK_RESPONSE_NO;

	priv = gpd->priv;

	if (!priv->started)
		return TRUE;

	dlg = gtk_message_dialog_new (GTK_WINDOW (priv->assistant), GTK_DIALOG_DESTROY_WITH_PARENT,
				      GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
				      _("Setup did not complete and settings will not\n"
					"be saved. Are you sure you want to quit?"));

	g_object_set_data (G_OBJECT (dlg), "_done", &dialog_done);
	g_object_set_data (G_OBJECT (dlg), "_response", &dialog_response);
	g_signal_connect (dlg, "response", G_CALLBACK (cancel_dialog_response_cb), NULL);
	gtk_window_present (GTK_WINDOW (dlg));

	while (!dialog_done)
		g_main_context_iteration (NULL, TRUE);

	if (dialog_response == GTK_RESPONSE_YES) {
		if (priv->handle1 > 0) {
			gnome_pilot_client_remove_request (priv->gpc, priv->handle1);
			priv->handle1 =-1;
		}
		if (priv->handle2 > 0) {
			gnome_pilot_client_remove_request (priv->gpc, priv->handle2);
			priv->handle2 =-1;
		}
		save_config_and_restart (priv->gpc, priv->orig_state);
		freePilotState (priv->state);
		priv->state = dupPilotState (priv->orig_state);

		gtk_window_destroy (GTK_WINDOW (dlg));

		return TRUE;
	}

	gtk_window_destroy (GTK_WINDOW (dlg));

	return FALSE;
}

static gboolean
check_cradle_settings (GnomePilotAssistant *gpd) 
{
	GnomePilotAssistantPrivate *priv;
	
	priv = gpd->priv;
	
	return check_editable (GTK_EDITABLE (priv->device_name))
		&& check_editable (GTK_EDITABLE (priv->device_port));
}

static gboolean
check_pilot_settings (GnomePilotAssistant *gpd) 
{
	GnomePilotAssistantPrivate *priv;
	
	priv = gpd->priv;
	
	return check_editable (GTK_EDITABLE (priv->pilot_username))
		&& check_editable (GTK_EDITABLE (priv->pilot_id));
}

static gboolean
gpd_delete_window (GtkWindow *window, gpointer user_data)
{
	return !cancel_dialog (GNOME_PILOT_ASSISTANT (user_data));
}

static void
gpd_canceled (GtkAssistant *assistant, gpointer user_data)
{
	GnomePilotAssistant *gpd = GNOME_PILOT_ASSISTANT (user_data);
	if (cancel_dialog (gpd)) {
		if (gpd->priv->loop)
			g_main_loop_quit (gpd->priv->loop);
	}
}

static void
gpd_page_prepare (GtkAssistant *assistant, GtkWidget *page,
    gpointer user_data)
{
	GnomePilotAssistant *gpd = GNOME_PILOT_ASSISTANT (user_data);
	gint pageid = gtk_assistant_get_current_page(assistant);
	gboolean ready;

	switch (pageid) {
	case GPD_PAGE_WELCOME:
	case GPD_PAGE_ERROR:
		/* intro page, carry straight on. */
		break;
	case GPD_PAGE_CRADLE:
		/* Device settings: check if widgets already filled in */
		ready = check_cradle_settings (gpd);
		gtk_assistant_set_page_complete (assistant,
		    gpd->priv->page_cradle, ready);
		break;
	case GPD_PAGE_PILOT_ONE:
		/* PDA Identification aka "page_pilot1" */
		gpd_cancel_sync(gpd); // deal with 'back' behaviour from page_sync
		break;
	case GPD_PAGE_SYNC:
		/* Initial Sync aka "page_sync" */
		gpd_sync_page_prepare(gpd);
		break;
	case GPD_PAGE_PILOT_TWO:
		/* PDA Attributes aka "page_pilot2" */
		gtk_assistant_set_page_complete (assistant,
		    gpd->priv->page_pilot_two, TRUE);
		break;
	case GPD_PAGE_FINISH:
		g_signal_connect   (G_OBJECT (gpd->priv->assistant), "close",
		    G_CALLBACK (gpd_finish_page_finished), gpd);

	default:
		break;
	}
}

static gint
gpd_forward_page(gint current_page, gpointer user_data)
{
	GnomePilotAssistant *gpd = GNOME_PILOT_ASSISTANT (user_data);

	switch (current_page)
	{
	case GPD_PAGE_WELCOME:
		return 1;
	case GPD_PAGE_CRADLE:
		// forward_page is called for various reasons, it seems,
		// by GtkAssistant (e.g. figuring out how the assistant
		// is wired)
		if (gtk_assistant_get_page_complete (
			GTK_ASSISTANT (gpd->priv->assistant),
			    gtk_assistant_get_nth_page (
				GTK_ASSISTANT (gpd->priv->assistant),
			        GPD_PAGE_CRADLE)
			)) {
			if (gpd_cradle_page_next(gpd)) {
				gtk_label_set_text (
				    GTK_LABEL (gpd->priv->page_error),
				    gpd->priv->errstr);
				return GPD_PAGE_ERROR;
			} else {
				return GPD_PAGE_PILOT_ONE;
			}
		} else {
			return GPD_PAGE_PILOT_ONE;
		}
		break;
	case GPD_PAGE_ERROR:
		/* never allowed forward from this page... */
		return GPD_PAGE_PILOT_ONE;
		break;
	case GPD_PAGE_PILOT_ONE:
		/* PDA Identification aka "druidpage_pilot1" */
		return GPD_PAGE_SYNC;
		break;
	case GPD_PAGE_SYNC:
		/* Initial Sync aka "druidpage_sync" */
		return GPD_PAGE_PILOT_TWO;
	case GPD_PAGE_PILOT_TWO:
		/* PDA Attributes aka "druidpage_pilot2" */
		if (gpd_pilot_page_two_next(gpd)) {
			gtk_label_set_text (
			    GTK_LABEL (gpd->priv->page_error),
				gpd->priv->errstr);
			return GPD_PAGE_ERROR;
		} else {
			return GPD_PAGE_FINISH;
		}
		break;
	case GPD_PAGE_FINISH:
		return GPD_PAGE_FINISH;
	default:
		return -1;
	}
}




static gboolean
gpd_cradle_page_next (GnomePilotAssistant *gpd)
{
	GnomePilotAssistantPrivate *priv;
	GPilotDevice *tmp_device;
	gboolean result;

	priv = gpd->priv;

	/* check the device settings */
	tmp_device = gpilot_device_new();
	read_device_config(G_OBJECT(gpd), tmp_device);
	if (priv->errstr != NULL) {
		g_free(priv->errstr);
		priv->errstr = NULL;
	}
	result = check_device_settings(tmp_device, &priv->errstr);
	g_free(tmp_device->name);
	g_free(tmp_device->port);
	if (!result) {
		gtk_label_set_text (GTK_LABEL (priv->page_error), priv->errstr);
		/* cancel proceeding to next page */
		/*gtk_assistant_set_page_complete (
		    GTK_ASSISTANT (priv->assistant),
		    gtk_assistant_get_nth_page (
			GTK_ASSISTANT (priv->assistant), 1),
			FALSE);*/
		return TRUE;
	}
	priv->started = TRUE;

	return FALSE;
}

static void
gpd_sync_page_prepare (GnomePilotAssistant *gpd)
{
	GnomePilotAssistantPrivate *priv;
	GNOME_Pilot_UserInfo user;
	gchar *text, *location;

	priv = gpd->priv;

	read_device_config (G_OBJECT (gpd), priv->device);
	
	if (priv->state->devices == NULL)
		priv->state->devices = g_list_append (priv->state->devices, priv->device);

	if (gtk_check_button_get_active(GTK_CHECK_BUTTON (priv->pilot_info_no))) {
		/* do send_to_pilot */
		read_pilot_config (G_OBJECT (gpd), priv->pilot);
		location = priv->device->type == PILOT_DEVICE_NETWORK ?
		    "netsync" : (priv->device->type == PILOT_DEVICE_BLUETOOTH ?
			"bluetooth" : priv->device->port);
		text = g_strdup_printf (_("About to send the following data to the PDA.\n"
				       "Owner Name: %s\nPDA ID: %d\n"
				       "Please put PDA in %s (%s) and press HotSync button."),
					priv->pilot->pilot_username,
					priv->pilot->pilot_id,
					priv->device->name,
					location);

		save_config_and_restart (priv->gpc, priv->state);

		user.userID = priv->pilot->pilot_id;
		user.username = priv->pilot->pilot_username;

		gnome_pilot_client_set_user_info (priv->gpc,
						  priv->device->name,
						  user,
						  FALSE,
						  GNOME_Pilot_IMMEDIATE,
						  0,
						  &priv->handle1);
                if (priv->handle1 <= 0) {
                        error_dialog (GTK_WINDOW (priv->assistant), _("Failed sending request to gpilotd"));
                        return;
                }
	} else {
		/* do get_from_pilot */
		location = priv->device->type == PILOT_DEVICE_NETWORK ?
		    "netsync" : (priv->device->type == PILOT_DEVICE_BLUETOOTH ?
			"bluetooth" : priv->device->port);
		text = g_strdup_printf (_("About to retrieve Owner Name and "
					    "ID from the PDA.\n"
					    "Please put PDA in %s (%s) and press "
					    "HotSync button."),
		    priv->device->name,
		    location);

		save_config_and_restart (priv->gpc, priv->state);

		gnome_pilot_client_get_user_info (priv->gpc, priv->device->name, GNOME_Pilot_IMMEDIATE, 0, &priv->handle1);
		gnome_pilot_client_get_system_info (priv->gpc, priv->device->name, GNOME_Pilot_IMMEDIATE, 0, &priv->handle2);

                if (priv->handle1 <= 0 || priv->handle2 <= 0) {
                        error_dialog (GTK_WINDOW (priv->assistant), _("Failed sending request to gpilotd"));
                        return;
                }
	}
	gtk_label_set_text (GTK_LABEL (priv->sync_label), text);
        g_free (text);

	/* disable NEXT until we've synced */
	gtk_assistant_set_page_complete (GTK_ASSISTANT(priv->assistant),
	    priv->page_sync, FALSE);
}

static gboolean
gpd_cancel_sync (GnomePilotAssistant *gpd)
{
	GnomePilotAssistantPrivate *priv;
	gboolean need_restart = FALSE;

	priv = gpd->priv;
	
	if (priv->handle1 > 0) {
		gnome_pilot_client_remove_request (priv->gpc, priv->handle1);
		priv->handle1 = -1;
		need_restart = TRUE;
	}
	if (priv->handle2 > 0) {
		gnome_pilot_client_remove_request (priv->gpc, priv->handle2);
		priv->handle2 = -1;
		need_restart = TRUE;
	}
	if (need_restart)
		save_config_and_restart (priv->gpc, priv->orig_state);
	return FALSE;
}

static gboolean
gpd_pilot_page_two_next (GnomePilotAssistant *gpd)
{
	GnomePilotAssistantPrivate *priv;
	
	priv = gpd->priv;

	if(priv->errstr != NULL) {
		g_free(priv->errstr);
		priv->errstr = NULL;
	}
	return (!(check_base_directory (gtk_editable_get_text (GTK_EDITABLE (priv->pilot_basedir)), &priv->errstr)
		&& check_pilot_charset (gtk_editable_get_text (GTK_EDITABLE (priv->pilot_charset)), &priv->errstr)
		));
}

static void
gpd_finish_page_finished (GtkAssistant *assistant, gpointer data)
{
	GnomePilotAssistant *gpd = GNOME_PILOT_ASSISTANT (data);
	GnomePilotAssistantPrivate *priv;
	
	priv = gpd->priv;
	
	read_pilot_config (G_OBJECT (gpd), priv->pilot);
	priv->state->pilots = g_list_append (priv->state->pilots, priv->pilot);
	
	save_config_and_restart (priv->gpc, priv->state);
	
	priv->finished = TRUE;

	if (priv->loop)
		g_main_loop_quit (priv->loop);
}

static void
gpd_device_info_check (GtkEditable *editable, gpointer user_data)
{
	GnomePilotAssistant *gpd = GNOME_PILOT_ASSISTANT (user_data);
	GnomePilotAssistantPrivate *priv;
	gboolean ready;
	
	priv = gpd->priv;
	
	ready = check_cradle_settings (gpd);
	gtk_assistant_set_page_complete (GTK_ASSISTANT (priv->assistant),
	    priv->page_cradle, ready);
}

static void
gpd_pilot_info_check (GtkEditable *editable, gpointer user_data)
{
	GnomePilotAssistant *gpd = GNOME_PILOT_ASSISTANT (user_data);
	GnomePilotAssistantPrivate *priv;
	gboolean ready = TRUE;
	
	priv = gpd->priv;
	
	if (gtk_check_button_get_active(GTK_CHECK_BUTTON (priv->pilot_info_no)))
		ready = check_pilot_settings (gpd);

	gtk_assistant_set_page_complete (GTK_ASSISTANT (priv->assistant),
	    priv->page_pilot_one, ready);
}

static void
gpd_pilot_name_check (GtkEditable *editable, gpointer user_data)
{
	GnomePilotAssistant *gpd = GNOME_PILOT_ASSISTANT (user_data);
	GnomePilotAssistantPrivate *priv;
	
	priv = gpd->priv;
	gtk_assistant_set_page_complete (
	    GTK_ASSISTANT (priv->assistant),
		priv->page_pilot_two,
		check_editable (GTK_EDITABLE (priv->pilot_name)));
	return;
}

static void
gpd_pilot_info_button (GtkToggleButton *toggle, gpointer user_data)
{
	GnomePilotAssistant *gpd = GNOME_PILOT_ASSISTANT (user_data);
	GnomePilotAssistantPrivate *priv;
	gboolean ready = TRUE;
	gboolean active;

	priv = gpd->priv;

	active = gtk_check_button_get_active (GTK_CHECK_BUTTON (toggle));
	gtk_widget_set_sensitive (priv->pilot_info, active);
	if (active)
		ready = check_pilot_settings (gpd);

	gtk_assistant_set_page_complete (GTK_ASSISTANT (priv->assistant),
	    priv->page_pilot_one, ready);
}

static void 
gpd_request_completed (GnomePilotClient* client, const gchar *id, gint handle, gpointer user_data) 
{
	GnomePilotAssistant *gpd = GNOME_PILOT_ASSISTANT (user_data);
	GnomePilotAssistantPrivate *priv;
	
	priv = gpd->priv;

	if (handle == priv->handle1)
		priv->handle1 = -1;
	else if (handle == priv->handle2)
		priv->handle2 = -1;
	else
		return;

	if (priv->handle1 == -1 && priv->handle2 == -1) {
		gtk_assistant_set_page_complete (GTK_ASSISTANT (priv->assistant),
		    priv->page_sync, TRUE);
	}
}

static void 
gpd_userinfo_requested (GnomePilotClient *gpc, const gchar *device, const GNOME_Pilot_UserInfo *user, gpointer user_data) 
{
	GnomePilotAssistant *gpd = GNOME_PILOT_ASSISTANT (user_data);
	GnomePilotAssistantPrivate *priv;
	gchar *text;
	gchar buf[20];
	
	priv = gpd->priv;
	
	g_message ("device %s sent userinfo", device);
	g_message ("user->userID   = %lu", user->userID);
	g_message ("user->username = %s", user->username);

	priv->pilot->pilot_id = user->userID;

	if (priv->pilot->pilot_username) 
		g_free (priv->pilot->pilot_username);
	priv->pilot->pilot_username = g_strdup (user->username);

	text = g_strdup_printf (_("Successfully retrieved Owner Name and ID from PDA.\n"
				  "Owner Name: %s\nPDA ID: %d"),
				priv->pilot->pilot_username,
				priv->pilot->pilot_id);
	gtk_label_set_text (GTK_LABEL (priv->sync_label), text);

	gtk_editable_set_text (GTK_EDITABLE (priv->pilot_username), priv->pilot->pilot_username);
	g_snprintf (buf, sizeof (buf), "%d", priv->pilot->pilot_id);
	gtk_editable_set_text (GTK_EDITABLE (priv->pilot_id), buf);
	g_free (text);

	gtk_assistant_set_page_complete (GTK_ASSISTANT (priv->assistant),
	    priv->page_sync, TRUE);

	priv->handle1 = priv->handle2 = -1;
}

static void 
gpd_system_info_requested (GnomePilotClient *gpc,
			    const gchar *device,
			    const GNOME_Pilot_SysInfo *sysinfo,
			    gpointer user_data) 
{
	GnomePilotAssistant *gpd = GNOME_PILOT_ASSISTANT (user_data);
	GnomePilotAssistantPrivate *priv;
	
	priv = gpd->priv;
	
	g_message ("device %s sent sysinfo", device);
	g_message ("sysinfo->creation   = %ld", sysinfo->creation);
	g_message ("sysinfo->romVersion = 0x%lx", sysinfo->romVersion);

	priv->pilot->creation = sysinfo->creation;
	priv->pilot->romversion = sysinfo->romVersion;
}

static void
gpd_dispose (GObject *object)
{
	GnomePilotAssistant *gpd = GNOME_PILOT_ASSISTANT (object);
	GnomePilotAssistantPrivate *priv;
	
	priv = gpd->priv;

	gtk_window_destroy (GTK_WINDOW (priv->assistant));
	g_object_unref (priv->ui);

	g_signal_handlers_disconnect_matched (priv->gpc,
	    G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, object);
}

static void
network_device_toggled_callback (GtkCheckButton *btn, void *data)
{
	GnomePilotAssistant *gpd = (GnomePilotAssistant *)data;
	GnomePilotAssistantPrivate *priv;
	int type;

	priv = gpd->priv;

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

	set_widget_visibility_by_type(gpd, type);
}

static void
set_widget_visibility_by_type(GnomePilotAssistant *gpd, int type) {
	GnomePilotAssistantPrivate *priv;

	gboolean enable_extra_widgets = (type != PILOT_DEVICE_NETWORK &&
	    type != PILOT_DEVICE_BLUETOOTH);

	priv = gpd->priv;

	gtk_widget_set_sensitive(priv->device_port_combo,
	    enable_extra_widgets);
	gtk_widget_set_sensitive(priv->device_port_label,
	    enable_extra_widgets);
	gtk_widget_set_sensitive(priv->device_speed,
	    enable_extra_widgets);
	gtk_widget_set_sensitive(priv->device_speed_label,
	    enable_extra_widgets);
}
