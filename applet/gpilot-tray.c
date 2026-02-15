/*
 * gpilot-tray.c — System tray application for gnome-pilot
 *
 * Standalone tray icon using org.kde.StatusNotifierItem D-Bus protocol.
 * Replaces the GNOME Panel applet (pilot.c) which requires libpanelapplet-4.0.
 * Context menu exported via libdbusmenu-glib.
 *
 * Copyright (C) 1998-2001 Free Software Foundation
 * Copyright (C) 2024 Free Software Foundation
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <config.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include <libdbusmenu-glib/server.h>
#include <libdbusmenu-glib/menuitem.h>
#include <libdbusmenu-glib/client.h>

#include "gpilot-daemon.h"
#include "gpilot-applet-progress.h"
#include "gnome-pilot-client.h"
#include "gpilot-tray-sni.h"

/* ── State machine (same as pilot.c) ─────────────────────────── */

typedef enum {
	INITIALISING,
	PAUSED,
	CONNECTING_TO_DAEMON,
	SYNCING,
	WAITING,
	NUM_STATES
} state;

/* Icon names — basename without .png extension, looked up via IconThemePath */
static const char *icon_names[] = {
	"sync_broken",    /* INITIALISING */
	"sync_paused",    /* PAUSED */
	"sync_icon",      /* CONNECTING_TO_DAEMON */
	"syncing_icon",   /* SYNCING */
	"sync_icon",      /* WAITING */
};

/* ── Properties (from pilot.c) ───────────────────────────────── */

typedef struct {
	GList  *pilot_ids;
	GList  *cradles;
	gchar  *exec_when_clicked;
	gboolean popups;
} pilot_properties;

/* ── Main application data ───────────────────────────────────── */

typedef struct {
	GApplication      *app;
	GSettings          *settings;
	pilot_properties    properties;
	state               curstate;

	/* GTK4 dialog widgets (progress dialog, restore, etc.) */
	GtkWidget *dialogWindow;
	GtkWidget *operationDialogWindow;
	GtkWidget *pb;
	GtkTextBuffer *message_buffer;
	GPilotAppletProgress *c_progress;

	guint   timeout_handler_id;
	gboolean druid_already_launched;

	GtkWidget *progressDialog;
	GtkWidget *sync_label;
	GtkWidget *overall_progress_bar;
	GtkWidget *conduit_progress_bar;
	GtkWidget *message_area;
	GtkWidget *cancel_button;
	GtkWidget *chooseDialog;
	GtkWidget *restoreDialog;
	GdkRGBA    errorColor;
	gchar     *ui_file;

	GnomePilotClient *gpc;

	/* SNI D-Bus */
	GDBusConnection     *bus;
	guint                sni_registration_id;
	guint                sni_bus_name_id;
	gchar               *sni_bus_name;
	const gchar         *current_icon_name;
	gchar               *tooltip_text;

	/* libdbusmenu */
	DbusmenuServer   *menu_server;
	DbusmenuMenuitem *mi_state;  /* Pause/Unpause — label changes */
} TrayApplet;

#define TRAY_APPLET(x) ((TrayApplet*)(x))

/* Forward declarations */
static void show_dialog (TrayApplet *self, GtkMessageType type, gchar *,...);
static void on_show_dialog_response (GtkDialog *dialog, gint response_id, gpointer data);
static void cancel_cb (GtkButton *button, gpointer whatever);
static void save_properties (TrayApplet *self);
static void load_properties (TrayApplet *self);
static gboolean timeout_cb (TrayApplet *self);
static void sni_emit_signal (TrayApplet *self, const gchar *signal_name);
static void sni_set_icon (TrayApplet *self);
static void sni_set_tooltip (TrayApplet *self, const gchar *text);
static void handle_client_error (TrayApplet *self);

#define GPILOTD_DRUID   "gpilotd-control-applet --assistant"
#define GPILOTD_CAPPLET "gpilotd-control-applet"
#define CONDUIT_CAPPLET "gpilotd-control-applet --cap-id=1"

/* ── UI helpers (from pilot.c) ───────────────────────────────── */

static GtkBuilder *
load_ui (const gchar *filename, const gchar *widget)
{
	GtkBuilder *ui = gtk_builder_new ();
	gtk_builder_add_from_file (ui, filename, NULL);
	return ui;
}

static GtkWidget *
get_widget (GtkBuilder *ui, const gchar *name)
{
	return GTK_WIDGET (gtk_builder_get_object (ui, name));
}

/* ── SNI icon / tooltip update ───────────────────────────────── */

static void
sni_set_icon (TrayApplet *self)
{
	self->current_icon_name = icon_names[self->curstate];
	sni_emit_signal (self, "NewIcon");
}

static void
sni_set_tooltip (TrayApplet *self, const gchar *text)
{
	g_free (self->tooltip_text);
	self->tooltip_text = g_strdup (text);
	sni_emit_signal (self, "NewToolTip");
}

/* replaces pilot_draw() */
static void
tray_draw (TrayApplet *self)
{
	sni_set_icon (self);
}

/* ── gpilotd callbacks (from pilot.c) ────────────────────────── */

static void
gpilotd_scroll_to_insert_mark (TrayApplet *applet)
{
	gtk_text_view_scroll_to_mark (
		GTK_TEXT_VIEW (applet->message_area),
		gtk_text_buffer_get_insert (applet->message_buffer),
		0.2, FALSE, 0.0, 0.0);
}

static void
gpilotd_connect_cb (GnomePilotClient *client,
                    const gchar *id,
                    const GNOME_Pilot_UserInfo *user,
                    gpointer user_data)
{
	gchar *buf;
	TrayApplet *applet = TRAY_APPLET (user_data);

	sni_set_tooltip (applet, _("Synchronizing..."));

	applet->curstate = SYNCING;
	g_message ("state = SYNCING");
	tray_draw (applet);

	if (applet->properties.popups == FALSE) return;

	if (applet->progressDialog == NULL) {
		gtk_window_set_default_icon_name ("gnome-palm");
		GtkBuilder *ui               = load_ui (applet->ui_file, "ProgressDialog");
		applet->progressDialog       = get_widget (ui, "ProgressDialog");
		applet->sync_label           = get_widget (ui, "sync_label");
		applet->message_area         = get_widget (ui, "message_area");
		applet->overall_progress_bar = get_widget (ui, "overall_progress_bar");
		applet->conduit_progress_bar = get_widget (ui, "conduit_progress_bar");
		applet->cancel_button        = get_widget (ui, "cancel_button");
		applet->message_buffer       = gtk_text_view_get_buffer (
			GTK_TEXT_VIEW (applet->message_area));

		g_signal_connect (G_OBJECT (applet->cancel_button), "clicked",
		                  G_CALLBACK (cancel_cb), applet);
	} else {
		gtk_text_buffer_set_text (applet->message_buffer, "", -1);
	}

	gtk_widget_set_sensitive (applet->cancel_button, FALSE);
	buf = g_strdup_printf (_("%s Synchronizing"), id);
	gtk_label_set_text (GTK_LABEL (applet->sync_label), buf);
	g_free (buf);
	gtk_window_present (GTK_WINDOW (applet->progressDialog));

	gtk_progress_bar_set_text (GTK_PROGRESS_BAR (applet->overall_progress_bar), _("Connecting..."));
	gtk_progress_bar_set_text (GTK_PROGRESS_BAR (applet->conduit_progress_bar), "");
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (applet->overall_progress_bar), 0);
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (applet->conduit_progress_bar), 0);

	gpilot_applet_progress_set_progress (applet->c_progress,
		GTK_PROGRESS_BAR (applet->conduit_progress_bar));
	gpilot_applet_progress_start (applet->c_progress);

	gdk_rgba_parse (&(applet->errorColor), "red");
}

static void
gpilotd_disconnect_cb (GnomePilotClient *client,
                       const gchar *id,
                       gpointer user_data)
{
	TrayApplet *applet = TRAY_APPLET (user_data);
	sni_set_tooltip (applet, _("Ready to synchronize"));

	applet->curstate = WAITING;
	g_message ("state = READY");

	tray_draw (applet);
	if (applet->properties.popups && applet->progressDialog != NULL) {
		gpilot_applet_progress_stop (applet->c_progress);
		gtk_widget_hide (applet->progressDialog);
	}
}

static void
gpilotd_request_completed (GnomePilotClient *client,
                           const gchar *id,
                           unsigned long handle,
                           gpointer user_data)
{
	TrayApplet *applet = TRAY_APPLET (user_data);

	if (applet->operationDialogWindow == NULL)
		return;

	gtk_dialog_response (GTK_DIALOG (applet->operationDialogWindow), GTK_RESPONSE_CLOSE);
	if (applet->properties.popups && applet->progressDialog != NULL) {
		gchar *txt = g_strdup_printf (_("Request %ld has been completed\n"), handle);
		gtk_text_buffer_insert_at_cursor (applet->message_buffer, txt, -1);
		g_free (txt);
		gpilotd_scroll_to_insert_mark (applet);
	}
}

static void
gpilotd_conduit_start (GnomePilotClient *client,
                       const gchar *id,
                       const gchar *conduit,
                       const gchar *db_name,
                       gpointer user_data)
{
	TrayApplet *applet = TRAY_APPLET (user_data);
	if (applet->properties.popups && applet->progressDialog != NULL) {
		gchar *txt = g_strdup_printf (_("%s Synchronizing : %s"), id, conduit);
		gtk_label_set_text (GTK_LABEL (applet->sync_label), txt);
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (applet->conduit_progress_bar), 0);
		gpilot_applet_progress_start (applet->c_progress);
		g_free (txt);
		txt = g_strdup_printf (_("%s: Started\n"), conduit);
		gtk_text_buffer_insert_at_cursor (applet->message_buffer, txt, -1);
		g_free (txt);
		gpilotd_scroll_to_insert_mark (applet);
	}
}

static void
gpilotd_conduit_end (GnomePilotClient *client,
                     const gchar *id,
                     const gchar *conduit,
                     gpointer user_data)
{
	TrayApplet *applet = TRAY_APPLET (user_data);
	if (applet->properties.popups && applet->progressDialog != NULL) {
		gchar *txt = g_strdup_printf (_("%s Finished : %s"), id, conduit);
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (applet->conduit_progress_bar), 1.0);
		gpilot_applet_progress_start (applet->c_progress);
		gtk_label_set_text (GTK_LABEL (applet->sync_label), txt);
		g_free (txt);
		txt = g_strdup_printf (_("%s: Ended\n"), conduit);
		gtk_text_buffer_insert_at_cursor (applet->message_buffer, txt, -1);
		g_free (txt);
		gtk_progress_bar_set_text (GTK_PROGRESS_BAR (applet->conduit_progress_bar), "");
		gpilotd_scroll_to_insert_mark (applet);
	}
}

static void
gpilotd_conduit_progress (GnomePilotClient *client,
                          const gchar *id,
                          const gchar *conduit,
                          guint current,
                          guint total,
                          gpointer user_data)
{
	TrayApplet *applet = TRAY_APPLET (user_data);
	gdouble cur_f = (gdouble)current / (gdouble)total;
	gchar *buf = NULL;

	if (applet->properties.popups && applet->progressDialog != NULL) {
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (applet->conduit_progress_bar), cur_f);
		buf = g_strdup_printf (_("%d of %d records"), current, total);
		gtk_progress_bar_set_text (GTK_PROGRESS_BAR (applet->conduit_progress_bar), buf);
		g_free (buf);
		gpilot_applet_progress_stop (applet->c_progress);
	}
	g_main_context_iteration (NULL, FALSE);
}

static void
gpilotd_overall_progress (GnomePilotClient *client,
                          const gchar *id,
                          guint current,
                          guint total,
                          gpointer user_data)
{
	TrayApplet *applet = TRAY_APPLET (user_data);
	gdouble cur_f = (gdouble)current / (gdouble)total;

	if (applet->properties.popups && applet->progressDialog != NULL) {
		gchar *buf = g_strdup_printf (_("Database %d of %d"), current, total);
		gtk_progress_bar_set_text (GTK_PROGRESS_BAR (applet->overall_progress_bar), buf);
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (applet->overall_progress_bar), cur_f);
	}
	g_main_context_iteration (NULL, FALSE);
}

static void
gpilotd_conduit_message (GnomePilotClient *client,
                         const gchar *id,
                         const gchar *conduit,
                         const gchar *message,
                         gpointer user_data)
{
	TrayApplet *applet = TRAY_APPLET (user_data);
	if (applet->properties.popups && applet->progressDialog != NULL) {
		gchar *txt = g_strdup_printf ("%s: %s\n", conduit, message);
		gtk_text_buffer_insert_at_cursor (applet->message_buffer, txt, -1);
		g_free (txt);
		gpilotd_scroll_to_insert_mark (applet);
	}
	g_main_context_iteration (NULL, FALSE);
}

static void
gpilotd_daemon_message (GnomePilotClient *client,
                        const gchar *id,
                        const gchar *conduit,
                        const gchar *message,
                        gpointer user_data)
{
	TrayApplet *applet = TRAY_APPLET (user_data);
	if (applet->properties.popups && applet->progressDialog != NULL) {
		gchar *txt = g_strdup_printf ("%s\n", message);
		gtk_text_buffer_insert_at_cursor (applet->message_buffer, txt, -1);
		g_free (txt);
		gpilotd_scroll_to_insert_mark (applet);
	}
	g_main_context_iteration (NULL, FALSE);
}

static void
gpilotd_daemon_error (GnomePilotClient *client,
                      const gchar *id,
                      const gchar *message,
                      gpointer user_data)
{
	TrayApplet *applet = TRAY_APPLET (user_data);
	if (applet->properties.popups && applet->progressDialog != NULL) {
		gchar *txt = g_strdup_printf ("Error: %s\n", message);
		gtk_text_buffer_insert_at_cursor (applet->message_buffer, txt, -1);
		g_free (txt);
		gpilotd_scroll_to_insert_mark (applet);
	}
}

static void
gpilotd_conduit_error (GnomePilotClient *client,
                       const gchar *id,
                       const gchar *conduit,
                       const gchar *message,
                       gpointer user_data)
{
	TrayApplet *applet = TRAY_APPLET (user_data);
	if (applet->properties.popups && applet->progressDialog != NULL) {
		GtkTextIter iter;
		GtkTextTag *tag;
		gchar *txt = g_strdup_printf ("%s: %s\n", conduit, message);
		gtk_text_buffer_get_end_iter (applet->message_buffer, &iter);
		tag = gtk_text_buffer_create_tag (applet->message_buffer, NULL,
		                                  "foreground-rgba", &(applet->errorColor),
		                                  NULL);
		gtk_text_buffer_insert_with_tags (applet->message_buffer,
		                                  &iter, txt, -1, tag, NULL);
		g_free (txt);
		gpilotd_scroll_to_insert_mark (applet);
	}
}

static void
handle_client_error (TrayApplet *self)
{
	if (self->curstate == SYNCING) {
		show_dialog (self, GTK_MESSAGE_WARNING,
		    _("PDA is currently synchronizing.\nPlease wait for it to finish."));
	} else {
		self->curstate = INITIALISING;
		g_message ("state = INITIALISING");
		sni_set_tooltip (self,
		    _("Not connected. Please restart daemon."));
		tray_draw (self);
		show_dialog (self, GTK_MESSAGE_ERROR,
		    _("Not connected to gpilotd.\nPlease restart daemon."));
	}
}

/* ── Menu rebuild for pause state label ──────────────────────── */

static void tray_build_menu (TrayApplet *self);

static void
gpilotd_daemon_pause (GnomePilotClient *client,
                      gboolean on_off,
                      gpointer user_data)
{
	TrayApplet *applet = TRAY_APPLET (user_data);

	if (on_off) {
		if (applet->curstate == WAITING) {
			applet->curstate = PAUSED;
			sni_set_tooltip (applet, _("Daemon paused..."));
			g_message ("state = PAUSED");
		} else {
			handle_client_error (applet);
		}
	} else {
		applet->curstate = WAITING;
		sni_set_tooltip (applet, _("Ready to synchronize"));
		g_message ("state = READY");
	}

	/* Rebuild menu to toggle Pause/Unpause label */
	tray_build_menu (applet);

	tray_draw (applet);
}

/* ── Dialogs (from pilot.c) ──────────────────────────────────── */

static void
on_show_dialog_response (GtkDialog *dialog, gint response_id, gpointer data)
{
	gtk_window_destroy (GTK_WINDOW (dialog));
}

static void
show_dialog (TrayApplet *self, GtkMessageType type, gchar *mesg, ...)
{
	char *tmp;
	va_list ap;

	va_start (ap, mesg);
	tmp = g_strdup_vprintf (mesg, ap);

	self->dialogWindow = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL,
	    type, GTK_BUTTONS_OK, "%s", tmp);
	g_signal_connect (self->dialogWindow, "response",
	                  G_CALLBACK (on_show_dialog_response), NULL);
	gtk_window_present (GTK_WINDOW (self->dialogWindow));
	g_free (tmp);
	va_end (ap);
}

static void
cancel_cb (GtkButton *button, gpointer applet)
{
	if (TRAY_APPLET (applet)->progressDialog != NULL)
		gtk_widget_hide (TRAY_APPLET (applet)->progressDialog);
}

/* ── Properties load/save ────────────────────────────────────── */

static void
load_properties (TrayApplet *self)
{
	g_free (self->properties.exec_when_clicked);
	self->properties.exec_when_clicked =
		g_settings_get_string (self->settings, "exec-when-clicked");
	self->properties.popups =
		g_settings_get_boolean (self->settings, "pop-ups");
}

static void
save_properties (TrayApplet *self)
{
	if (self->properties.exec_when_clicked)
		g_settings_set_string (self->settings, "exec-when-clicked",
		                       self->properties.exec_when_clicked);
	g_settings_set_boolean (self->settings, "pop-ups", self->properties.popups);
}

/* ── Pilot IDs helper ────────────────────────────────────────── */

static GList *
get_pilot_ids_from_gpilotd (TrayApplet *self)
{
	GList *pilots = NULL;
	gnome_pilot_client_get_pilots (self->gpc, &pilots);
	return pilots;
}

/* ── pilot execute program ───────────────────────────────────── */

static void
pilot_execute_program (TrayApplet *self, const gchar *str)
{
	g_return_if_fail (str != NULL);
	if (!g_spawn_command_line_async (str, NULL))
		show_dialog (self, GTK_MESSAGE_WARNING,
		    _("Execution of %s failed"), str);
}

/* ── Async pick-pilot (from pilot.c) ─────────────────────────── */

typedef void (*PickPilotCallback) (TrayApplet *self, const gchar *pilot_name, gpointer user_data);

typedef struct {
	TrayApplet       *self;
	PickPilotCallback callback;
	gpointer          user_data;
} PickPilotData;

static void
activate_pilot_menu (GtkComboBox *widget, gpointer data)
{
	TrayApplet *applet = TRAY_APPLET (data);
	gchar *label;
	GtkTreeIter iter;

	gtk_combo_box_get_active_iter (widget, &iter);
	gtk_tree_model_get (gtk_combo_box_get_model (widget), &iter, 0, &label, -1);
	g_object_set_data (G_OBJECT (applet->chooseDialog), "pilot", (gpointer)label);
}

static void
on_choose_pilot_response (GtkDialog *dialog, gint response_id, gpointer data)
{
	PickPilotData *ppd = data;
	gchar *pilot = NULL;

	if (response_id == 0)
		pilot = (gchar *)g_object_get_data (G_OBJECT (ppd->self->chooseDialog), "pilot");
	gtk_widget_set_visible (ppd->self->chooseDialog, FALSE);

	if (ppd->callback)
		ppd->callback (ppd->self, pilot, ppd->user_data);
	g_free (ppd);
}

static void
pick_pilot_async (TrayApplet *self, PickPilotCallback callback, gpointer user_data)
{
	if (self->properties.pilot_ids) {
		if (g_list_length (self->properties.pilot_ids) == 1) {
			gchar *pilot = (gchar *)self->properties.pilot_ids->data;
			if (callback)
				callback (self, pilot, user_data);
		} else {
			GList *tmp;
			GtkWidget *combo;
			GtkListStore *list_store;
			GtkCellRenderer *renderer;
			PickPilotData *ppd;

			if (self->chooseDialog == NULL) {
				GtkBuilder *ui = load_ui (self->ui_file, "ChoosePilot");
				self->chooseDialog = get_widget (ui, "ChoosePilot");
				combo = get_widget (ui, "pilot_combo");
				g_object_set_data (G_OBJECT (self->chooseDialog), "pilot_combo", combo);
			} else {
				combo = g_object_get_data (G_OBJECT (self->chooseDialog), "pilot_combo");
			}

			list_store = gtk_list_store_new (1, G_TYPE_STRING);
			gtk_combo_box_set_model (GTK_COMBO_BOX (combo), GTK_TREE_MODEL (list_store));
			gtk_cell_layout_clear (GTK_CELL_LAYOUT (combo));
			renderer = gtk_cell_renderer_text_new ();
			gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo), renderer, TRUE);
			gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (combo), renderer, "text", 0);
			tmp = self->properties.pilot_ids;
			while (tmp != NULL) {
				GtkTreeIter iter;
				gtk_list_store_append (list_store, &iter);
				gtk_list_store_set (list_store, &iter, 0, (gchar *)tmp->data, -1);
				tmp = tmp->next;
			}

			g_signal_connect (G_OBJECT (combo), "changed",
			                  G_CALLBACK (activate_pilot_menu), self);
			gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);
			g_object_set_data (G_OBJECT (self->chooseDialog), "pilot",
			                   self->properties.pilot_ids->data);

			ppd = g_new0 (PickPilotData, 1);
			ppd->self = self;
			ppd->callback = callback;
			ppd->user_data = user_data;
			g_signal_connect (self->chooseDialog, "response",
			                  G_CALLBACK (on_choose_pilot_response), ppd);
			gtk_window_present (GTK_WINDOW (self->chooseDialog));
		}
	} else {
		if (callback)
			callback (self, NULL, user_data);
	}
}

/* ── Restore dialog (from pilot.c) ───────────────────────────── */

static void
complete_restore (GnomePilotClient *client, const gchar *id,
                  unsigned long handle, gpointer user_data)
{
	TrayApplet *applet = user_data;
	gtk_window_destroy (GTK_WINDOW (applet->operationDialogWindow));
	applet->operationDialogWindow = NULL;
}

typedef struct {
	TrayApplet *applet;
	gchar      *pilot_name;
	int         handle;
	int         completed_id;
} RestoreOperationData;

static void
on_operation_dialog_response (GtkDialog *dialog, gint response_id, gpointer data)
{
	RestoreOperationData *rop = data;
	TrayApplet *applet = rop->applet;

	if (applet->operationDialogWindow != NULL)
		gtk_window_destroy (GTK_WINDOW (applet->operationDialogWindow));
	applet->operationDialogWindow = NULL;

	if (response_id == GTK_RESPONSE_CANCEL) {
		g_message (_("cancelling %d"), (int)rop->handle);
		gnome_pilot_client_remove_request (applet->gpc, rop->handle);
	}
	g_signal_handler_disconnect (applet->gpc, rop->completed_id);
	g_free (rop->pilot_name);
	g_free (rop);
}

typedef struct {
	TrayApplet *applet;
	gchar      *pilot_name;
	GtkWidget  *dir_entry;
} RestoreDialogData;

static void
on_restore_dialog_response (GtkDialog *dialog, gint response_id, gpointer data)
{
	RestoreDialogData *rd = data;
	TrayApplet *applet = rd->applet;

	if (response_id == GTK_RESPONSE_OK) {
		int id;
		int handle;
		gchar *backupdir;

		backupdir = g_strdup (gtk_editable_get_text (GTK_EDITABLE (rd->dir_entry)));
		gtk_window_destroy (GTK_WINDOW (applet->restoreDialog));
		applet->restoreDialog = NULL;

		if (backupdir == NULL || strlen (backupdir) == 0) {
			show_dialog (applet, GTK_MESSAGE_WARNING,
			    _("No directory to restore from."));
			g_free (backupdir);
			g_free (rd->pilot_name);
			g_free (rd);
			return;
		}

		id = gnome_pilot_client_connect__completed_request (applet->gpc, complete_restore, applet);
		if (gnome_pilot_client_restore (applet->gpc, rd->pilot_name,
			backupdir, GNOME_Pilot_IMMEDIATE, 0, &handle) == GPILOTD_OK) {
			RestoreOperationData *rop = g_new0 (RestoreOperationData, 1);
			rop->applet = applet;
			rop->pilot_name = g_strdup (rd->pilot_name);
			rop->handle = handle;
			rop->completed_id = id;

			applet->operationDialogWindow =
			    gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL,
			        GTK_MESSAGE_OTHER, GTK_BUTTONS_CANCEL, "%s",
			        _("Press synchronize on the cradle to restore\n"
			          " or cancel the operation."));
			g_signal_connect (applet->operationDialogWindow, "response",
			                  G_CALLBACK (on_operation_dialog_response), rop);
			gtk_window_present (GTK_WINDOW (applet->operationDialogWindow));
		} else {
			show_dialog (applet, GTK_MESSAGE_WARNING,
			    _("Restore request failed"));
			g_signal_handler_disconnect (applet->gpc, id);
		}
		g_free (backupdir);
	} else {
		gtk_window_destroy (GTK_WINDOW (applet->restoreDialog));
		applet->restoreDialog = NULL;
	}
	g_free (rd->pilot_name);
	g_free (rd);
}

static void
restore_with_pilot (TrayApplet *applet, const gchar *pilot_name, gpointer unused)
{
	int i;
	guint pilot_id;
	GtkBuilder *ui;
	GtkWidget *dir_entry, *id_entry, *frame;
	GtkWidget *device_combo;
	GtkListStore *list_store;
	GtkCellRenderer *renderer;
	GList *list;
	gchar *buf = NULL;
	gchar *backupdir_tmp = NULL;
	RestoreDialogData *rd;

	if (pilot_name == NULL)
		return;

	if (gnome_pilot_client_get_pilot_base_dir_by_name (applet->gpc, pilot_name, &buf) == GPILOTD_OK) {
		backupdir_tmp = g_strdup_printf ("%s", buf);
		g_free (buf);
	} else {
		handle_client_error (applet);
		return;
	}
	if (gnome_pilot_client_get_pilot_id_by_name (applet->gpc, pilot_name, &pilot_id) != GPILOTD_OK) {
		handle_client_error (applet);
		g_free (backupdir_tmp);
		return;
	}

	ui = load_ui (applet->ui_file, "RestoreDialog");
	applet->restoreDialog = get_widget (ui, "RestoreDialog");

	dir_entry = get_widget (ui, "dir_entry");
	gtk_editable_set_text (GTK_EDITABLE (dir_entry), backupdir_tmp);
	g_free (backupdir_tmp);

	frame = get_widget (ui, "main_frame");
	buf = g_strdup_printf (_("Restoring %s"), pilot_name);
	gtk_frame_set_label (GTK_FRAME (frame), buf);
	g_free (buf);

	id_entry = get_widget (ui, "pilotid_entry");
	buf = g_strdup_printf ("%d", pilot_id);
	gtk_editable_set_text (GTK_EDITABLE (id_entry), buf);
	g_free (buf);

	applet->properties.cradles = NULL;
	gnome_pilot_client_get_cradles (applet->gpc, &(applet->properties.cradles));
	list = applet->properties.cradles;

	device_combo = get_widget (ui, "device_combo");
	list_store = gtk_list_store_new (1, G_TYPE_STRING);
	gtk_combo_box_set_model (GTK_COMBO_BOX (device_combo), GTK_TREE_MODEL (list_store));
	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (device_combo), renderer, TRUE);
	gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (device_combo), renderer, "text", 0);

	i = 0;
	while (list) {
		GtkTreeIter iter;
		gtk_list_store_append (list_store, &iter);
		gtk_list_store_set (list_store, &iter, 0, (gchar *)list->data, -1);
		list = list->next;
		i++;
	}
	gtk_combo_box_set_active (GTK_COMBO_BOX (device_combo), 0);
	if (i <= 1) gtk_widget_set_sensitive (GTK_WIDGET (device_combo), FALSE);
	else gtk_widget_set_sensitive (GTK_WIDGET (device_combo), TRUE);

	rd = g_new0 (RestoreDialogData, 1);
	rd->applet = applet;
	rd->pilot_name = g_strdup (pilot_name);
	rd->dir_entry = dir_entry;
	g_signal_connect (applet->restoreDialog, "response",
	                  G_CALLBACK (on_restore_dialog_response), rd);
	gtk_window_present (GTK_WINDOW (applet->restoreDialog));
}

/* ── Menu action callbacks ───────────────────────────────────── */

static void
menu_restore_cb (DbusmenuMenuitem *mi, guint timestamp, gpointer user_data)
{
	TrayApplet *applet = TRAY_APPLET (user_data);
	if (applet->curstate != WAITING) {
		handle_client_error (applet);
		return;
	}
	pick_pilot_async (applet, restore_with_pilot, NULL);
}

static void
menu_state_cb (DbusmenuMenuitem *mi, guint timestamp, gpointer user_data)
{
	TrayApplet *applet = TRAY_APPLET (user_data);
	GnomePilotClient *gpc = applet->gpc;

	if (applet->curstate == WAITING) {
		if (gnome_pilot_client_pause_daemon (gpc) != GPILOTD_OK)
			handle_client_error (applet);
	} else {
		if (gnome_pilot_client_unpause_daemon (gpc) != GPILOTD_OK)
			handle_client_error (applet);
	}
}

static void
do_restart_daemon (TrayApplet *applet)
{
	applet->curstate = INITIALISING;
	g_message ("state = INITIALISING");
	gnome_pilot_client_restart_daemon (applet->gpc);
	sni_set_tooltip (applet, _("Trying to connect to the GnomePilot Daemon"));
	applet->timeout_handler_id = g_timeout_add (1000, (GSourceFunc) timeout_cb, applet);
}

static void
on_restart_confirm_response (GtkDialog *dialog, gint response_id, gpointer data)
{
	TrayApplet *applet = TRAY_APPLET (data);
	gtk_window_destroy (GTK_WINDOW (dialog));

	if (response_id == GTK_RESPONSE_YES) {
		if (applet->progressDialog != NULL)
			gtk_widget_hide (applet->progressDialog);
		do_restart_daemon (applet);
	}
}

static void
menu_restart_cb (DbusmenuMenuitem *mi, guint timestamp, gpointer user_data)
{
	TrayApplet *applet = TRAY_APPLET (user_data);

	if (applet->curstate == SYNCING) {
		GtkWidget *dialog;
		dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL,
		    GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "%s",
		    _("PDA sync is currently in progress.\nAre you sure you want to restart daemon?"));
		g_signal_connect (dialog, "response",
		                  G_CALLBACK (on_restart_confirm_response), applet);
		gtk_window_present (GTK_WINDOW (dialog));
		return;
	}
	do_restart_daemon (applet);
}

static void
menu_log_cb (DbusmenuMenuitem *mi, guint timestamp, gpointer user_data)
{
	TrayApplet *applet = TRAY_APPLET (user_data);
	if (applet->progressDialog != NULL) {
		gtk_label_set_text (GTK_LABEL (applet->sync_label), "Last Sync Log");
		gtk_widget_set_sensitive (applet->cancel_button, TRUE);
		gtk_window_present (GTK_WINDOW (applet->progressDialog));
	} else {
		show_dialog (applet, GTK_MESSAGE_INFO,
		    _("There's no last sync on record."));
	}
}

static void
exec_on_click_changed_cb (GtkEventControllerFocus *controller, gpointer data)
{
	TrayApplet *self = data;
	GtkWidget *w = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (controller));
	const gchar *new_list = gtk_editable_get_text (GTK_EDITABLE (w));
	if (new_list) {
		g_free (self->properties.exec_when_clicked);
		self->properties.exec_when_clicked = g_strdup (new_list);
	}
	save_properties (self);
}

static void
toggle_notices_cb (GtkWidget *toggle, gpointer data)
{
	TrayApplet *self = data;
	self->properties.popups = gtk_check_button_get_active (GTK_CHECK_BUTTON (toggle));
	save_properties (self);
}

static void
response_cb (GtkDialog *dialog, gint id, gpointer data)
{
	if (id == GTK_RESPONSE_HELP) {
		gtk_show_uri (GTK_WINDOW (dialog), "ghelp:gnome-pilot", GDK_CURRENT_TIME);
		return;
	}
	gtk_widget_hide (GTK_WIDGET (dialog));
}

static void
menu_properties_cb (DbusmenuMenuitem *mi, guint timestamp, gpointer user_data)
{
	TrayApplet *self = TRAY_APPLET (user_data);
	GtkWidget *button, *entry, *dialog;
	GtkBuilder *ui;

	gtk_window_set_default_icon_name ("gnome-palm");
	ui = load_ui (self->ui_file, "PropertiesDialog");
	dialog = get_widget (ui, "PropertiesDialog");

	entry = get_widget (ui, "exec_entry");
	if (self->properties.exec_when_clicked)
		gtk_editable_set_text (GTK_EDITABLE (entry), self->properties.exec_when_clicked);
	{
		GtkEventController *focus_controller = gtk_event_controller_focus_new ();
		g_signal_connect (focus_controller, "leave",
		                  G_CALLBACK (exec_on_click_changed_cb), self);
		gtk_widget_add_controller (entry, focus_controller);
	}

	button = get_widget (ui, "notices_button");
	gtk_check_button_set_active (GTK_CHECK_BUTTON (button), self->properties.popups);
	g_signal_connect (G_OBJECT (button), "toggled",
	                  G_CALLBACK (toggle_notices_cb), self);

	g_signal_connect (G_OBJECT (dialog), "response",
	                  G_CALLBACK (response_cb), NULL);

	gtk_window_present (GTK_WINDOW (dialog));
}

static void
menu_help_cb (DbusmenuMenuitem *mi, guint timestamp, gpointer user_data)
{
	gtk_show_uri (NULL, "ghelp:gnome-pilot#pilot-applet", GDK_CURRENT_TIME);
}

static void
menu_about_cb (DbusmenuMenuitem *mi, guint timestamp, gpointer user_data)
{
	GtkWidget *about;
	const gchar *authors[] = {
		"Vadim Strizhevsky <vadim@optonline.net>",
		"Eskil Heyn Olsen, <eskil@eskil.dk>",
		"JP Rosevear <jpr@ximian.com>",
		"Chris Toshok <toshok@ximian.com>",
		"Frank Belew <frb@ximian.com>",
		"Matt Davey <mcdavey@mrao.cam.ac.uk>",
		"Halton Huo <haltonhuo@gnome.org>",
		NULL
	};

	gtk_window_set_default_icon_name ("gnome-palm");
	about = gtk_about_dialog_new ();
	gtk_about_dialog_set_program_name (GTK_ABOUT_DIALOG (about), _("gnome-pilot tray"));
	gtk_about_dialog_set_version (GTK_ABOUT_DIALOG (about), VERSION);
	gtk_about_dialog_set_copyright (GTK_ABOUT_DIALOG (about),
		_("Copyright 2000-2006 Free Software Foundation, Inc."));
	gtk_about_dialog_set_comments (GTK_ABOUT_DIALOG (about),
		_("A PalmOS PDA monitor.\n"));
	gtk_about_dialog_set_authors (GTK_ABOUT_DIALOG (about), (const gchar **)authors);
	gtk_about_dialog_set_translator_credits (GTK_ABOUT_DIALOG (about),
		"Translations by the GNOME Translation Project");
	gtk_about_dialog_set_logo_icon_name (GTK_ABOUT_DIALOG (about), "palm-pilot-sync");

	g_signal_connect (about, "response",
	                  G_CALLBACK (on_show_dialog_response), NULL);
	gtk_window_present (GTK_WINDOW (about));
}

/* ── DbusmenuServer menu construction ────────────────────────── */

static void
tray_build_menu (TrayApplet *self)
{
	DbusmenuMenuitem *root, *mi;

	root = dbusmenu_menuitem_new ();
	dbusmenu_menuitem_property_set (root, DBUSMENU_MENUITEM_PROP_LABEL, "root");

	/* Restore... */
	mi = dbusmenu_menuitem_new ();
	dbusmenu_menuitem_property_set (mi, DBUSMENU_MENUITEM_PROP_LABEL, _("Restore..."));
	g_signal_connect (mi, DBUSMENU_MENUITEM_SIGNAL_ITEM_ACTIVATED,
	                  G_CALLBACK (menu_restore_cb), self);
	dbusmenu_menuitem_child_append (root, mi);

	/* Pause / Unpause */
	mi = dbusmenu_menuitem_new ();
	if (self->curstate == PAUSED)
		dbusmenu_menuitem_property_set (mi, DBUSMENU_MENUITEM_PROP_LABEL, _("Unpause"));
	else
		dbusmenu_menuitem_property_set (mi, DBUSMENU_MENUITEM_PROP_LABEL, _("Pause"));
	g_signal_connect (mi, DBUSMENU_MENUITEM_SIGNAL_ITEM_ACTIVATED,
	                  G_CALLBACK (menu_state_cb), self);
	dbusmenu_menuitem_child_append (root, mi);
	self->mi_state = mi;

	/* Restart Daemon */
	mi = dbusmenu_menuitem_new ();
	dbusmenu_menuitem_property_set (mi, DBUSMENU_MENUITEM_PROP_LABEL, _("Restart Daemon"));
	g_signal_connect (mi, DBUSMENU_MENUITEM_SIGNAL_ITEM_ACTIVATED,
	                  G_CALLBACK (menu_restart_cb), self);
	dbusmenu_menuitem_child_append (root, mi);

	/* Sync Log */
	mi = dbusmenu_menuitem_new ();
	dbusmenu_menuitem_property_set (mi, DBUSMENU_MENUITEM_PROP_LABEL, _("Sync Log"));
	g_signal_connect (mi, DBUSMENU_MENUITEM_SIGNAL_ITEM_ACTIVATED,
	                  G_CALLBACK (menu_log_cb), self);
	dbusmenu_menuitem_child_append (root, mi);

	/* ── Separator ── */
	mi = dbusmenu_menuitem_new ();
	dbusmenu_menuitem_property_set (mi, DBUSMENU_MENUITEM_PROP_TYPE,
	                                DBUSMENU_CLIENT_TYPES_SEPARATOR);
	dbusmenu_menuitem_child_append (root, mi);

	/* Settings... */
	mi = dbusmenu_menuitem_new ();
	dbusmenu_menuitem_property_set (mi, DBUSMENU_MENUITEM_PROP_LABEL, _("Settings..."));
	g_signal_connect (mi, DBUSMENU_MENUITEM_SIGNAL_ITEM_ACTIVATED,
	                  G_CALLBACK (menu_properties_cb), self);
	dbusmenu_menuitem_child_append (root, mi);

	/* Help */
	mi = dbusmenu_menuitem_new ();
	dbusmenu_menuitem_property_set (mi, DBUSMENU_MENUITEM_PROP_LABEL, _("Help"));
	g_signal_connect (mi, DBUSMENU_MENUITEM_SIGNAL_ITEM_ACTIVATED,
	                  G_CALLBACK (menu_help_cb), self);
	dbusmenu_menuitem_child_append (root, mi);

	/* About */
	mi = dbusmenu_menuitem_new ();
	dbusmenu_menuitem_property_set (mi, DBUSMENU_MENUITEM_PROP_LABEL, _("About"));
	g_signal_connect (mi, DBUSMENU_MENUITEM_SIGNAL_ITEM_ACTIVATED,
	                  G_CALLBACK (menu_about_cb), self);
	dbusmenu_menuitem_child_append (root, mi);

	dbusmenu_server_set_root (self->menu_server, root);
}

/* ── Timeout / state machine (from pilot.c) ──────────────────── */

static gboolean
timeout_cb (TrayApplet *self)
{
	if (self->curstate == INITIALISING) {
		tray_draw (self);

		if (gnome_pilot_client_connect_to_daemon (self->gpc) == GPILOTD_OK) {
			self->curstate = CONNECTING_TO_DAEMON;
			g_message ("state = CONNECTING_TO_DAEMON");
			tray_draw (self);
		}
	}

	if (self->curstate == CONNECTING_TO_DAEMON) {
		if (self->properties.pilot_ids) {
			GList *tmp = self->properties.pilot_ids;
			while (tmp) {
				g_free ((gchar *)tmp->data);
				tmp = g_list_next (tmp);
			}
			g_list_free (self->properties.pilot_ids);
		}

		self->properties.pilot_ids = get_pilot_ids_from_gpilotd (self);

		if (self->properties.pilot_ids == NULL) {
			sni_set_tooltip (self,
			    _("Not connected. Restart daemon to reconnect"));
			if (self->druid_already_launched == FALSE) {
				self->druid_already_launched = TRUE;
				pilot_execute_program (self, GPILOTD_DRUID);
			}
			self->curstate = INITIALISING;
			g_message ("state = INITIALISING");
			tray_draw (self);
		} else {
			sni_set_tooltip (self, _("Ready to synchronize"));
			self->curstate = WAITING;
			g_message ("state = READY");
			tray_draw (self);
		}
	}

	switch (self->curstate) {
	case WAITING:
		if (gnome_pilot_client_noop (self->gpc) == GPILOTD_ERR_NOT_CONNECTED) {
			self->curstate = INITIALISING;
			g_message ("state = INITIALISING");
		}
		break;
	case SYNCING:
	case PAUSED:
	default:
		break;
	}

	return TRUE;
}

/* ══════════════════════════════════════════════════════════════
 * SNI D-Bus interface implementation
 * ══════════════════════════════════════════════════════════════ */

static void
sni_emit_signal (TrayApplet *self, const gchar *signal_name)
{
	if (self->bus == NULL || self->sni_registration_id == 0)
		return;

	GVariant *params = NULL;
	if (g_strcmp0 (signal_name, "NewStatus") == 0)
		params = g_variant_new ("(s)", "Active");

	g_dbus_connection_emit_signal (self->bus,
	    NULL,
	    SNI_OBJECT_PATH,
	    SNI_INTERFACE,
	    signal_name,
	    params,
	    NULL);
}

/* D-Bus method call handler */
static void
sni_method_call (GDBusConnection       *connection,
                 const gchar           *sender,
                 const gchar           *object_path,
                 const gchar           *interface_name,
                 const gchar           *method_name,
                 GVariant              *parameters,
                 GDBusMethodInvocation *invocation,
                 gpointer               user_data)
{
	TrayApplet *self = TRAY_APPLET (user_data);

	if (g_strcmp0 (method_name, "Activate") == 0) {
		/* Left-click: execute configured program */
		if (self->properties.exec_when_clicked &&
		    strlen (self->properties.exec_when_clicked) > 0) {
			pilot_execute_program (self, self->properties.exec_when_clicked);
		}
		g_dbus_method_invocation_return_value (invocation, NULL);
	} else if (g_strcmp0 (method_name, "ContextMenu") == 0) {
		/* Menu is handled via libdbusmenu, but some hosts call this */
		g_dbus_method_invocation_return_value (invocation, NULL);
	} else if (g_strcmp0 (method_name, "SecondaryActivate") == 0) {
		g_dbus_method_invocation_return_value (invocation, NULL);
	} else if (g_strcmp0 (method_name, "Scroll") == 0) {
		g_dbus_method_invocation_return_value (invocation, NULL);
	} else {
		g_dbus_method_invocation_return_error (invocation,
		    G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
		    "Unknown method %s", method_name);
	}
}

/* D-Bus property getter */
static GVariant *
sni_get_property (GDBusConnection  *connection,
                  const gchar      *sender,
                  const gchar      *object_path,
                  const gchar      *interface_name,
                  const gchar      *property_name,
                  GError          **error,
                  gpointer          user_data)
{
	TrayApplet *self = TRAY_APPLET (user_data);

	if (g_strcmp0 (property_name, "Category") == 0)
		return g_variant_new_string ("ApplicationStatus");

	if (g_strcmp0 (property_name, "Id") == 0)
		return g_variant_new_string ("gpilot-tray");

	if (g_strcmp0 (property_name, "Title") == 0)
		return g_variant_new_string ("GNOME Pilot");

	if (g_strcmp0 (property_name, "Status") == 0)
		return g_variant_new_string ("Active");

	if (g_strcmp0 (property_name, "WindowId") == 0)
		return g_variant_new_uint32 (0);

	if (g_strcmp0 (property_name, "IconName") == 0)
		return g_variant_new_string (self->current_icon_name ? self->current_icon_name : "sync_broken");

	if (g_strcmp0 (property_name, "IconThemePath") == 0)
		return g_variant_new_string (GNOME_ICONDIR);

	if (g_strcmp0 (property_name, "IconPixmap") == 0) {
		/* Empty array — we use icon names, not pixmaps */
		GVariantBuilder builder;
		g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(iiay)"));
		return g_variant_builder_end (&builder);
	}

	if (g_strcmp0 (property_name, "OverlayIconName") == 0)
		return g_variant_new_string ("");

	if (g_strcmp0 (property_name, "OverlayIconPixmap") == 0) {
		GVariantBuilder builder;
		g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(iiay)"));
		return g_variant_builder_end (&builder);
	}

	if (g_strcmp0 (property_name, "AttentionIconName") == 0)
		return g_variant_new_string ("");

	if (g_strcmp0 (property_name, "AttentionIconPixmap") == 0) {
		GVariantBuilder builder;
		g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(iiay)"));
		return g_variant_builder_end (&builder);
	}

	if (g_strcmp0 (property_name, "AttentionMovieName") == 0)
		return g_variant_new_string ("");

	if (g_strcmp0 (property_name, "ToolTip") == 0) {
		/* (sa(iiay)ss) — icon_name, icon_pixmap[], title, body */
		GVariantBuilder pixmap_builder;
		g_variant_builder_init (&pixmap_builder, G_VARIANT_TYPE ("a(iiay)"));
		return g_variant_new ("(sa(iiay)ss)",
		    self->current_icon_name ? self->current_icon_name : "",
		    &pixmap_builder,
		    "GNOME Pilot",
		    self->tooltip_text ? self->tooltip_text : "");
	}

	if (g_strcmp0 (property_name, "ItemIsMenu") == 0)
		return g_variant_new_boolean (FALSE);

	if (g_strcmp0 (property_name, "Menu") == 0)
		return g_variant_new_object_path (SNI_MENU_OBJECT_PATH);

	g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
	    "Unknown property %s", property_name);
	return NULL;
}

static const GDBusInterfaceVTable sni_vtable = {
	sni_method_call,
	sni_get_property,
	NULL  /* set_property — all read-only */
};

/* ── SNI registration on session bus ─────────────────────────── */

static void
sni_register_with_watcher (TrayApplet *self)
{
	GError *error = NULL;
	GVariant *result;

	result = g_dbus_connection_call_sync (self->bus,
	    SNI_WATCHER_BUS_NAME,
	    SNI_WATCHER_OBJ_PATH,
	    SNI_WATCHER_INTERFACE,
	    "RegisterStatusNotifierItem",
	    g_variant_new ("(s)", self->sni_bus_name),
	    NULL,
	    G_DBUS_CALL_FLAGS_NONE,
	    -1, NULL, &error);

	if (error) {
		g_warning ("Failed to register with StatusNotifierWatcher: %s", error->message);
		g_error_free (error);
	} else {
		g_message ("Registered with StatusNotifierWatcher as %s", self->sni_bus_name);
		if (result)
			g_variant_unref (result);
	}
}

static void
on_sni_bus_name_acquired (GDBusConnection *connection,
                          const gchar     *name,
                          gpointer         user_data)
{
	TrayApplet *self = TRAY_APPLET (user_data);
	g_message ("Acquired D-Bus name: %s", name);
	sni_register_with_watcher (self);
}

static void
on_sni_bus_name_lost (GDBusConnection *connection,
                      const gchar     *name,
                      gpointer         user_data)
{
	g_warning ("Lost D-Bus name: %s", name);
}

static void
sni_setup (TrayApplet *self)
{
	GError *error = NULL;
	GDBusNodeInfo *node_info;

	self->bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
	if (error) {
		g_error ("Cannot connect to session bus: %s", error->message);
		/* g_error is fatal */
	}

	/* Parse introspection XML */
	node_info = g_dbus_node_info_new_for_xml (sni_introspection_xml, &error);
	if (error) {
		g_error ("Failed to parse SNI introspection XML: %s", error->message);
	}

	/* Register object */
	self->sni_registration_id = g_dbus_connection_register_object (
	    self->bus,
	    SNI_OBJECT_PATH,
	    node_info->interfaces[0],
	    &sni_vtable,
	    self,
	    NULL,
	    &error);

	if (error) {
		g_error ("Failed to register SNI object: %s", error->message);
	}

	g_dbus_node_info_unref (node_info);

	/* Create DbusmenuServer on /MenuBar */
	self->menu_server = dbusmenu_server_new (SNI_MENU_OBJECT_PATH);
	tray_build_menu (self);

	/* Acquire a unique bus name */
	self->sni_bus_name = g_strdup_printf ("org.kde.StatusNotifierItem-%d-1", getpid ());
	self->sni_bus_name_id = g_bus_own_name_on_connection (
	    self->bus,
	    self->sni_bus_name,
	    G_BUS_NAME_OWNER_FLAGS_NONE,
	    on_sni_bus_name_acquired,
	    on_sni_bus_name_lost,
	    self,
	    NULL);
}

/* ── GApplication activate ───────────────────────────────────── */

static void
app_activate (GApplication *app, gpointer user_data)
{
	TrayApplet *self = TRAY_APPLET (user_data);

	/* Locate UI file */
	self->ui_file = "./pilot-applet.ui";
	if (!g_file_test (self->ui_file, G_FILE_TEST_EXISTS)) {
		self->ui_file = g_build_filename (UIDATADIR, "pilot-applet.ui", NULL);
	}
	if (!g_file_test (self->ui_file, G_FILE_TEST_EXISTS)) {
		g_warning ("Cannot find pilot-applet.ui");
	}

	/* GSettings */
	self->settings = g_settings_new ("org.gnome.GnomePilot.applet");
	load_properties (self);

	/* Progress animation helper */
	self->c_progress = GPILOT_APPLET_PROGRESS (gpilot_applet_progress_new ());

	/* Initial state */
	self->curstate = INITIALISING;
	self->current_icon_name = icon_names[INITIALISING];
	self->tooltip_text = g_strdup (_("Trying to connect to the GnomePilot Daemon"));
	g_message ("state = INITIALISING");

	/* GnomePilotClient */
	self->gpc = GNOME_PILOT_CLIENT (gnome_pilot_client_new ());

	gnome_pilot_client_connect__pilot_connect (self->gpc, gpilotd_connect_cb, self);
	gnome_pilot_client_connect__pilot_disconnect (self->gpc, gpilotd_disconnect_cb, self);
	gnome_pilot_client_connect__completed_request (self->gpc, gpilotd_request_completed, self);
	gnome_pilot_client_connect__start_conduit (self->gpc, gpilotd_conduit_start, self);
	gnome_pilot_client_connect__end_conduit (self->gpc, gpilotd_conduit_end, self);
	gnome_pilot_client_connect__progress_conduit (self->gpc, gpilotd_conduit_progress, self);
	gnome_pilot_client_connect__progress_overall (self->gpc, gpilotd_overall_progress, self);
	gnome_pilot_client_connect__error_conduit (self->gpc, gpilotd_conduit_error, self);
	gnome_pilot_client_connect__message_conduit (self->gpc, gpilotd_conduit_message, self);
	gnome_pilot_client_connect__message_daemon (self->gpc, gpilotd_daemon_message, self);
	gnome_pilot_client_connect__error_daemon (self->gpc, gpilotd_daemon_error, self);
	gnome_pilot_client_connect__daemon_pause (self->gpc, gpilotd_daemon_pause, self);

	/* Set up SNI on D-Bus */
	sni_setup (self);

	/* Start polling for daemon connection */
	self->timeout_handler_id = g_timeout_add (1000, (GSourceFunc) timeout_cb, self);

	/* Hold the application alive (no windows) */
	g_application_hold (app);
}

/* ── main ────────────────────────────────────────────────────── */

int
main (int argc, char *argv[])
{
	TrayApplet *self;
	GApplication *app;
	int status;

	gtk_init ();

	self = g_new0 (TrayApplet, 1);

	app = g_application_new ("org.gnome.GnomePilot.Tray",
	                         G_APPLICATION_FLAGS_NONE);
	self->app = app;

	g_signal_connect (app, "activate", G_CALLBACK (app_activate), self);

	status = g_application_run (app, argc, argv);

	/* Cleanup */
	if (self->timeout_handler_id)
		g_source_remove (self->timeout_handler_id);
	if (self->sni_bus_name_id)
		g_bus_unown_name (self->sni_bus_name_id);
	if (self->sni_registration_id && self->bus)
		g_dbus_connection_unregister_object (self->bus, self->sni_registration_id);
	g_free (self->sni_bus_name);
	g_free (self->tooltip_text);
	if (self->settings)
		g_object_unref (self->settings);
	if (self->menu_server)
		g_object_unref (self->menu_server);
	g_object_unref (app);
	g_free (self);

	return status;
}
