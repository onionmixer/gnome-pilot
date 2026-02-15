/* util.c
 *
 * Copyright (C) 1999-2000 Free Software Foundation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors: Eskil Heyn Olsen
 *          Vadim Strizhevsky
 *
 */

#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>
#include <errno.h>
#include "util.h"
#include <iconv.h>
#include <gtk/gtk.h>

static const guint speedList[] = {9600, 19200, 38400, 57600, 115200, 0};
#define  DEFAULT_SPEED_INDEX  3  /* Default to 57600 */

void
fill_speed_combo (GtkComboBox *option_combo)
{
        gint i = 0;
	GtkTreeModel *model;
	GtkTreeIter iter;

        gchar *text;

        g_return_if_fail (option_combo != NULL);
        g_return_if_fail (GTK_IS_COMBO_BOX (option_combo));

	model = gtk_combo_box_get_model (option_combo);
	if (model != NULL)
		/* tree model not empty -- don't reinitialise */
		return;

	model = GTK_TREE_MODEL(gtk_list_store_new (1, G_TYPE_STRING));
	i = 0;
        while (speedList[i] != 0) {
		text = g_strdup_printf ("%d", speedList[i]);
		gtk_list_store_append (GTK_LIST_STORE (model), &iter);
		gtk_list_store_set (GTK_LIST_STORE (model), &iter, 0, text, -1);
		g_free(text);
                i++;
        }
	gtk_combo_box_set_model (option_combo, model);

	gtk_cell_layout_clear(GTK_CELL_LAYOUT(option_combo));
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (option_combo),
	    renderer, TRUE);
	gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (option_combo),
	    renderer, "text", 0);

        gtk_combo_box_set_active(option_combo, DEFAULT_SPEED_INDEX);
}

/* return false if an invalid speed was supplied */
gboolean
speed_combo_set_speed (GtkComboBox *option_combo, int speed)
{
	gint i;

	i = 0;
	while(speedList[i] != 0) {
		if (speedList[i] == speed) {
			gtk_combo_box_set_active(option_combo, i);
			return TRUE;
		}
		i++;
	}
	return FALSE;
}

int
speed_combo_get_speed (GtkComboBox *option_combo)
{
	return speedList[gtk_combo_box_get_active(option_combo)];
}


void 
fill_conduit_sync_type_combo (GtkComboBox *option_combo, ConduitState *state)
{
	int current = 0;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GList *tmp;
	int index;

        g_return_if_fail (option_combo != NULL);
        g_return_if_fail (GTK_IS_COMBO_BOX (option_combo));

	model = gtk_combo_box_get_model (option_combo);

	if (model != NULL) {
		/* we have to set combo box active row to match current sync state,
		 * so just as easy to start again.
		 */
		gtk_list_store_clear(GTK_LIST_STORE(model));
	} else {
		model = GTK_TREE_MODEL(gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT));
	}

	gtk_list_store_append (GTK_LIST_STORE (model), &iter);
	gtk_list_store_set (GTK_LIST_STORE (model), &iter, 0, _("Disabled"),
	    1, GnomePilotConduitSyncTypeNotSet, -1);

	tmp = state->valid_synctypes;
	if (tmp == NULL && state->default_sync_type == GnomePilotConduitSyncTypeCustom ) {
		gtk_list_store_append (GTK_LIST_STORE (model), &iter);
		gtk_list_store_set (GTK_LIST_STORE (model), &iter, 0, _("Enabled"),
		    1, state->default_sync_type, -1);
		if (state->sync_type == state->default_sync_type) 
			current = 1;
	} else {
		for (index = 0; tmp != NULL; tmp = tmp->next, index++) {		
			gtk_list_store_append (GTK_LIST_STORE (model), &iter);
			gtk_list_store_set (GTK_LIST_STORE (model), &iter,
			    0, sync_type_to_str(GPOINTER_TO_INT (tmp->data)),
			    1, GPOINTER_TO_INT(tmp->data), -1);
			if (GPOINTER_TO_INT(tmp->data) == state->sync_type) 
				current = index + 1;
		}
	}
	gtk_combo_box_set_model (option_combo, model);

	gtk_cell_layout_clear(GTK_CELL_LAYOUT(option_combo));
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (option_combo),
	    renderer, TRUE);
	gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (option_combo),
	    renderer, "text", 0);

        gtk_combo_box_set_active(option_combo, current);
}

void 
fill_conduit_first_sync_type_combo (GtkComboBox *option_combo, ConduitState *state)
{
	int current = 0;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GList *tmp = NULL;
	int index;

        g_return_if_fail (option_combo != NULL);
        g_return_if_fail (GTK_IS_COMBO_BOX (option_combo));

	model = gtk_combo_box_get_model (option_combo);
	if (model != NULL) {
		/* we have to set combo box active row to match current sync state,
		 * so just as easy to start again.
		 */
		gtk_list_store_clear(GTK_LIST_STORE(model));
	} else {
		model = GTK_TREE_MODEL(gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT));
	}

	model = GTK_TREE_MODEL(gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT));

	gtk_list_store_append (GTK_LIST_STORE (model), &iter);
	gtk_list_store_set (GTK_LIST_STORE (model), &iter, 0, _("None"),
	    1, GnomePilotConduitSyncTypeNotSet, -1);

	if (state->default_sync_type != GnomePilotConduitSyncTypeCustom) {
		tmp = state->valid_synctypes;
	}
	for (index = 0; tmp != NULL; tmp = tmp->next, index++) {
		gtk_list_store_append (GTK_LIST_STORE (model), &iter);
		gtk_list_store_set (GTK_LIST_STORE (model), &iter,
		    0, sync_type_to_str(GPOINTER_TO_INT (tmp->data)),
		    1, GPOINTER_TO_INT(tmp->data), -1);
		if (GPOINTER_TO_INT(tmp->data) == state->sync_type) 
			current = index + 1;
	}
	gtk_combo_box_set_model (option_combo, model);

	gtk_cell_layout_clear(GTK_CELL_LAYOUT(option_combo));
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (option_combo),
	    renderer, TRUE);
	gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (option_combo),
	    renderer, "text", 0);

        gtk_combo_box_set_active(option_combo, current);
}

void
show_popup_at (GtkWidget *popover, GtkWidget *parent, double x, double y)
{
	GdkRectangle rect = { (int)x, (int)y, 1, 1 };
	gtk_popover_set_pointing_to (GTK_POPOVER (popover), &rect);
	gtk_widget_set_parent (popover, parent);
	gtk_popover_popup (GTK_POPOVER (popover));
}

void 
error_dialog (GtkWindow *parent, gchar *mesg, ...) 
{
	GtkWidget *dlg;
	char *tmp;
	va_list ap;

	va_start (ap,mesg);
	tmp = g_strdup_vprintf (mesg,ap);

	dlg = gtk_message_dialog_new (parent, GTK_DIALOG_DESTROY_WITH_PARENT,
	    GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", tmp);
	g_signal_connect (dlg, "response", G_CALLBACK (gtk_window_destroy), NULL);
	gtk_window_present (GTK_WINDOW (dlg));
	
	va_end (ap);
	g_free (tmp);
}

const char *
get_default_pilot_charset(void) {
	const char *pc;

	if ((pc = getenv("PILOT_CHARSET")) == NULL) {
		pc = GPILOT_DEFAULT_CHARSET;
	}
	return pc;
}

static void
choose_dialog_response_cb (GtkDialog *dlg, gint response_id, gpointer user_data)
{
	gboolean *done = g_object_get_data (G_OBJECT (dlg), "_done");
	gint *resp = g_object_get_data (G_OBJECT (dlg), "_response");
	*resp = response_id;
	*done = TRUE;
}

static GPilotDevice *
real_choose_pilot_dialog (PilotState *state)
{
	GtkBuilder *ui;
	GtkWidget *dlg;
	GtkWidget *device_combo;
	GList *tmp;
	GtkListStore *list_store;
	GtkCellRenderer *renderer;
	GPilotDevice *dev;
        guint index;
	gchar *objects[] = {"ChooseDevice", NULL};
	
	ui = gtk_builder_new ();
	gtk_builder_add_objects_from_file (ui, UIDATADIR "/gpilotd-capplet.ui", objects, NULL);
	dlg = GTK_WIDGET (gtk_builder_get_object (ui,"ChooseDevice"));
	device_combo = GTK_WIDGET (gtk_builder_get_object (ui, "device_combo"));

	g_object_set_data (G_OBJECT (dlg), "device_combo", device_combo);

	list_store = gtk_list_store_new (1, G_TYPE_STRING);
	gtk_combo_box_set_model (GTK_COMBO_BOX (device_combo), GTK_TREE_MODEL (list_store));
	renderer = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (device_combo), renderer, TRUE);
	gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (device_combo), renderer, "text", 0);

	tmp = state->devices;
	while (tmp != NULL){
		GtkTreeIter iter;

		gtk_list_store_append (list_store, &iter);
		dev =(GPilotDevice*)tmp->data;
		if(dev->type == PILOT_DEVICE_NETWORK) {
			gtk_list_store_set (list_store, &iter, 0, "[network]", -1);
		} else if(dev->type == PILOT_DEVICE_BLUETOOTH) {
			gtk_list_store_set (list_store, &iter, 0, "[bluetooth]", -1);
		} else {
			gtk_list_store_set (list_store, &iter, 0, dev->port, -1);
		}
		tmp = tmp->next;
	}
	gtk_combo_box_set_active (GTK_COMBO_BOX (device_combo), 0);

	/* Run a nested main loop for the dialog */
	gboolean dialog_done = FALSE;
	gint dialog_response = GTK_RESPONSE_CANCEL;
	g_object_set_data (G_OBJECT (dlg), "_done", &dialog_done);
	g_object_set_data (G_OBJECT (dlg), "_response", &dialog_response);
	g_signal_connect (dlg, "response", G_CALLBACK (choose_dialog_response_cb), NULL);
	gtk_window_present (GTK_WINDOW (dlg));
	while (!dialog_done)
		g_main_context_iteration (NULL, TRUE);
	if (GTK_RESPONSE_OK == dialog_response) {
		index = gtk_combo_box_get_active(GTK_COMBO_BOX(device_combo));
		dev = g_list_nth_data(state->devices, index);
	} else {
		dev = NULL;
	}
	gtk_window_destroy (GTK_WINDOW (dlg));

	return dev;
}

GPilotDevice *
choose_pilot_dialog (PilotState *state)
{
	GPilotDevice *dev = NULL;

	if (state->devices == NULL)
		error_dialog (NULL, _("You must have at least one device setup"));
	else if (g_list_length (state->devices) == 1)
		dev = (GPilotDevice*)state->devices->data;
	else
		dev = real_choose_pilot_dialog (state);

	return dev;
}

GPilotPilot *
get_default_pilot (PilotState *state)
{
	GPilotPilot *pilot = g_new0 (GPilotPilot, 1);
	
	pilot->pilot_username = g_strdup(g_get_real_name ());
	pilot->pilot_id = getuid ();
	pilot->name = next_pilot_name (state);
	pilot->sync_options.basedir = g_build_filename (g_get_home_dir (),
							pilot->name, NULL);
	pilot->pilot_charset =
	    g_strdup(get_default_pilot_charset());

	return pilot;
}

GPilotDevice *
get_default_device (PilotState *state)
{
	GPilotDevice *device = g_new0 (GPilotDevice, 1);
	
	device->name = next_cradle_name (state);
	device->port = g_strdup ("usb:");
	/* XXX */
	device->speed = speedList[DEFAULT_SPEED_INDEX];
	device->type = PILOT_DEVICE_USB_VISOR;
	device->timeout = 0;
	
	return device;
}

void
insert_numeric_callback (GtkEditable *editable, const gchar *text,
			 gint len, gint *position, void *data)
{
	gint i;
	
	for (i =0; i<len; i++) {
		if (!isdigit (text[i])) {
			g_signal_stop_emission_by_name (G_OBJECT (editable), "insert_text");
			return;
		}
	}
}

void
insert_username_callback (GtkEditable *editable, const gchar *text,
			  gint len, gint *position, void *data)
{
	gunichar utf8_char;

	/* need to make sure that existing entry starts with a letter */
	/* since valid usernames must start with a letter             */
	if (*position == 0 && len > 0) {
		utf8_char = g_utf8_get_char_validated (text, -1);
		if (!g_unichar_isalpha (utf8_char)) {
			g_signal_stop_emission_by_name (G_OBJECT (editable), "insert_text");
		}
	} else {
		gchar *p = (char *) text; 
		/* rest of username can be alphanumeric */
		while (p && *p) {
			utf8_char = g_utf8_get_char_validated (p, -1);
			if (!g_unichar_isalnum (utf8_char) && !g_unichar_isspace (utf8_char)) {
				g_signal_stop_emission_by_name (G_OBJECT (editable), 
							     "insert_text");
			}
			p = g_utf8_find_next_char (p, NULL);
		}
	}
}

void
insert_device_callback (GtkEditable *editable, const gchar *text,
			gint len, gint *position, void *data)
{
	gint i;
	const gchar *curname;

	curname = gtk_editable_get_text (GTK_EDITABLE (editable));
	if (*curname == '\0' && len > 0) {
		if (text[0]!='/'
		    /* usb: pseudo-device is available from pilot-link 0.12.0 */
		    && text[0]!='u'
		    ) {
			g_signal_stop_emission_by_name (G_OBJECT (editable), "insert_text");
			return;
		} 
	} else {
		for (i =0;i<len;i++)
			if (!(isalnum (text[i]) || text[i]=='/' || text[i]==':')) {
				g_signal_stop_emission_by_name (G_OBJECT (editable), "insert_text");
				return;
			}
	}
}

gboolean
check_editable (GtkEditable *editable)
{
	gboolean test = TRUE;
	char *str;
	
	str = g_strdup (gtk_editable_get_text (GTK_EDITABLE (editable)));
	if (str == NULL || strlen (str) < 1)
		test = FALSE;
	g_free (str);

	return test;
}

/* find the next "Cradle#" name that is available for use */

static gint compare_device_name (GPilotDevice *device, gchar *name)
{
	return strcmp (device->name,name);
}

gchar *
next_cradle_name (PilotState *state)
{
	int i =0;
	gchar buf[16];
	
	sprintf (buf,"Cradle");
	
	while (g_list_find_custom (state->devices,buf,
				   (GCompareFunc)compare_device_name)!= NULL) {
		i++;
		sprintf (buf,"Cradle%d",i);
	}
	return g_strdup (buf);
}

/* find the next "MyPDA#" name that is available for use */

static gint 
compare_pilot_name (GPilotPilot *pilot, gchar *name)
{
	return strcmp (pilot->name, name);
}

gchar *
next_pilot_name (PilotState *state)
{
	int i =0;
	gchar buf[16];
	
	sprintf (buf,"MyPDA");
	
	while (g_list_find_custom (state->pilots, buf,
				   (GCompareFunc)compare_pilot_name)!= NULL) {
		i++;
		sprintf (buf,"MyPDA%d",i);
	}
	return g_strdup (buf);
}

const gchar* 
sync_type_to_str (GnomePilotConduitSyncType t) 
{
	switch (t) {
	case GnomePilotConduitSyncTypeSynchronize:    return _("Synchronize");
	case GnomePilotConduitSyncTypeCopyFromPilot:  return _("Copy from PDA");
	case GnomePilotConduitSyncTypeCopyToPilot:    return _("Copy to PDA");
	case GnomePilotConduitSyncTypeMergeFromPilot: return _("Merge from PDA");
	case GnomePilotConduitSyncTypeMergeToPilot:   return _("Merge to PDA");
	case GnomePilotConduitSyncTypeCustom: 
	case GnomePilotConduitSyncTypeNotSet:     
	default:                                      return _("Use conduit settings");
	}
}

const gchar* 
device_type_to_str (GPilotDeviceType t) 
{
	switch (t) {
	case PILOT_DEVICE_USB_VISOR: return _("USB");
	case PILOT_DEVICE_IRDA:      return _("IrDA");
	case PILOT_DEVICE_NETWORK:   return _("Network");
	case PILOT_DEVICE_BLUETOOTH: return _("Bluetooth");
	default:                     return _("Serial");
	}
}

const gchar *
display_sync_type_name (gboolean enabled, GnomePilotConduitSyncType sync_type)
{
	if (!enabled) 
		return _("Disabled");
	else if (sync_type == GnomePilotConduitSyncTypeCustom) 
		return _("Enabled");
	else 
		return sync_type_to_str (sync_type);
}

gboolean
check_pilot_info (GPilotPilot* pilot1, GPilotPilot *pilot2)
{
	if (pilot1->pilot_id == pilot2->pilot_id 
	    || !strcmp (pilot1->name, pilot2->name)) 
		return TRUE;

	return FALSE;
}

gboolean
check_device_info (GPilotDevice* device1, GPilotDevice *device2)
{
	if (!strcmp (device1->port, device2->port) || !strcmp (device1->name, device2->name)) 
		return TRUE;

	return FALSE;
}

gboolean
check_base_directory (const gchar *dir_name, gchar **errstr)
{
	gboolean ret = TRUE;
	/* check basedir validity */
	
	if (mkdir (dir_name, 0700) < 0 ) {
		struct stat buf;
		switch (errno) {
		case EEXIST: 
			stat (dir_name, &buf);
			if (S_ISDIR (buf.st_mode)) {  
				if (!(buf.st_mode & (S_IRUSR | S_IWUSR |S_IXUSR))) {
				    *errstr = g_strdup(_("The specified base directory exists but has the wrong permissions.\n"
					    "Please fix or choose another directory"));;
					ret = FALSE;
				}
			} else {
				*errstr = g_strdup (_("The specified base directory exists but is not a directory.\n"
						"Please make it a directory or choose another directory"));
				ret = FALSE;
			}
			break;
			
		case EACCES:
		    *errstr = g_strdup(_("It wasn't possible to create the specified base directory.\n"
					"Please verify the permitions on the specified path or choose another directory"));
			ret = FALSE;
			break;
		case ENOENT:
		    *errstr = g_strdup (_("The path specified for the base directory is invalid.\n"
					"Please choose another directory"));
			ret = FALSE;
			break;
		default:
		    *errstr = g_strdup(strerror (errno));
		    ret = FALSE;
		}
	}
	return ret;
}

/* Check charset is a valid iconv character set id.
 * return TRUE if it's valid, or FALSE otherwise.
 */
gboolean
check_pilot_charset (const gchar *charset, gchar **errstr)
{
	iconv_t cd;

	if (charset == NULL || *charset == '\0')
		return TRUE;
	cd = iconv_open(charset, "UTF8");
        if (cd == (iconv_t)-1) {
		*errstr = g_strdup (g_strdup_printf(_("`%s' is not a valid character set"
				  " identifier.\nPlease enter a valid"
				  " identifier or select from the available"
			    " options."), charset));
		
		return FALSE;
	}

	iconv_close(cd);
	return TRUE;
}

void
read_device_config (GObject *object, GPilotDevice* device)
{
	GtkWidget *port_entry, *speed_combo, *name_entry;
	GtkWidget *usb_radio, *irda_radio, *network_radio, *timeout_spinner, *bluetooth_radio;

	g_return_if_fail (device!= NULL);

	port_entry  = g_object_get_data (G_OBJECT (object), "port_entry");
	name_entry  = g_object_get_data (G_OBJECT (object), "name_entry");
	speed_combo = g_object_get_data (G_OBJECT (object), "speed_combo");
	usb_radio = g_object_get_data (G_OBJECT (object), "usb_radio");
	irda_radio = g_object_get_data (G_OBJECT (object), "irda_radio");
	network_radio = g_object_get_data (G_OBJECT (object), "network_radio");
	bluetooth_radio = g_object_get_data (G_OBJECT (object), "bluetooth_radio");
	timeout_spinner = g_object_get_data (G_OBJECT (object), "timeout_spinner");

	if (device->port)
		g_free (device->port);
	device->port = g_strdup (gtk_editable_get_text (GTK_EDITABLE (port_entry)));

	if (device->name)
		g_free (device->name);
	device->name = g_strdup (gtk_editable_get_text (GTK_EDITABLE (name_entry)));
	if (device->name == NULL) device->name = g_strdup ("Cradle");

	device->speed = speed_combo_get_speed(GTK_COMBO_BOX(speed_combo));

	device->type = PILOT_DEVICE_SERIAL;
	if (gtk_check_button_get_active (GTK_CHECK_BUTTON (usb_radio))) {
		device->type = PILOT_DEVICE_USB_VISOR;
	} else if (gtk_check_button_get_active (GTK_CHECK_BUTTON (irda_radio))) {
		device->type = PILOT_DEVICE_IRDA;
	} else if (gtk_check_button_get_active (GTK_CHECK_BUTTON (network_radio))) {
		device->type = PILOT_DEVICE_NETWORK;
	} else if (gtk_check_button_get_active (GTK_CHECK_BUTTON (bluetooth_radio))) {
		device->type = PILOT_DEVICE_BLUETOOTH;
	}
	
	device->timeout = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (timeout_spinner));
}

void
read_pilot_config (GObject *object, GPilotPilot *pilot)
{
	GtkWidget *id, *name, *pname, *basedir, *menu;
	GtkWidget *charset;
	const gchar *num;
	gint pilotid;
	g_return_if_fail (pilot!= NULL);

	id      = g_object_get_data (G_OBJECT (object), "pilotid");
	name    = g_object_get_data (G_OBJECT (object), "username");
	pname   = g_object_get_data (G_OBJECT (object), "pilotname");
	basedir = g_object_get_data (G_OBJECT (object), "basedir");
	charset = g_object_get_data (G_OBJECT (object), "charset");
	menu    = g_object_get_data (G_OBJECT (object), "sync_menu");
 	
	num = gtk_editable_get_text (GTK_EDITABLE (id));
	pilotid = strtol (num, NULL, 10);
	pilot->pilot_id = pilotid;

	if (pilot->pilot_username)
		g_free (pilot->pilot_username);
	pilot->pilot_username = g_strdup (gtk_editable_get_text (GTK_EDITABLE (name)));

	if (pilot->name)
		g_free (pilot->name);
	pilot->name = g_strdup (gtk_editable_get_text (GTK_EDITABLE (pname)));

	if (pilot->sync_options.basedir)
		g_free (pilot->sync_options.basedir);
	pilot->sync_options.basedir = g_strdup (gtk_editable_get_text (GTK_EDITABLE (basedir)));

	if (pilot->pilot_charset)
		g_free (pilot->pilot_charset);
	pilot->pilot_charset = g_strdup (gtk_editable_get_text (GTK_EDITABLE (charset)));
}

void 
save_config_and_restart (GnomePilotClient *gpc, PilotState *state) 
{
	savePilotState (state);
	/* FORCE the gpilotd to reread the settings */
	gnome_pilot_client_reread_config (gpc);
}

/* returns TRUE if the device passes basic sanity checks,
 * otherwise, returns FALSE and an error-string in *errstr.
 */
gboolean
check_device_settings (GPilotDevice *device, char **errstr)
{	
	struct stat buf;
	char *usbdevicesfile_str ="/proc/bus/usb/devices";
	char *sysfs_dir = "/sys/bus/usb/devices";

	/* device->port is ignored for network and bluetooth syncs */
	if (strcmp(device->port, "usb:") == 0 && device->type != PILOT_DEVICE_NETWORK
	    && device->type != PILOT_DEVICE_BLUETOOTH
	    && device->type != PILOT_DEVICE_USB_VISOR) {
		*errstr = g_strdup (_("Device 'usb:' is only valid for devices of type USB"));
		return FALSE;
	}

	if (device->type == PILOT_DEVICE_SERIAL) {
		g_message ("checking rw on %s", device->port);
		if (access (device->port, R_OK|W_OK)) {
			*errstr = g_strdup_printf ("%s\n%s (%s)\n%s",
					       _("Read/Write permissions failed on"),
					       device->name, device->port,
					       _("Check the permissions on the device and retry"));
			return FALSE;
		}
#ifdef linux
	} else if (device->type == PILOT_DEVICE_USB_VISOR) {
		/* check sysfs or usbfs is mounted */
		if(stat(sysfs_dir, &buf) != 0 &&
		    ((stat (usbdevicesfile_str, &buf) != 0 &&
		      stat ("/proc/bus/usb/devices_please-use-sysfs-instead", &buf) != 0) ||
		    !(S_ISREG(buf.st_mode)) ||
		    !(buf.st_mode & S_IRUSR))) {
			*errstr = g_strdup_printf (
			    _("Failed to find directory %s or read file %s.  "
				"Check that usbfs or sysfs is mounted."),
			    sysfs_dir,
			    usbdevicesfile_str);
			return FALSE;
		}
#endif /* linux */
	} 

	return TRUE;
}
