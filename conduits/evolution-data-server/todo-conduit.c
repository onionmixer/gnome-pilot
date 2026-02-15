/*
 * Evolution calendar - ToDo Conduit
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Authors:
 *		Eskil Heyn Olsen <deity@eskil.dk>
 *      JP Rosevear <jpr@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include <libecal/libecal.h>
#include <libedataserver/libedataserver.h>
#include <pi-source.h>
#include <pi-socket.h>
#include <pi-dlp.h>
#include <pi-todo.h>
#include <libical/icaltypes.h>
#include <gnome-pilot-conduit.h>
#include <gnome-pilot-conduit-sync-abs.h>
#include <gnome-pilot-conduit-management.h>
#include <gnome-pilot-conduit-config.h>
#include <e-pilot-map.h>
#include <e-pilot-settings.h>
#include <e-pilot-util.h>
#include <libecalendar-common-conduit.h>

GnomePilotConduit * conduit_get_gpilot_conduit (guint32);
void conduit_destroy_gpilot_conduit (GnomePilotConduit*);

#define CONDUIT_VERSION "0.1.6"

#define DEBUG_TODOCONDUIT 1
/* #undef DEBUG_TODOCONDUIT */

#ifdef DEBUG_TODOCONDUIT
#define LOG(x) x
#else
#define LOG(x)
#endif

#define WARN g_warning
#define INFO g_message

/* Compatibility: icaltimetype_to_tm was removed from libical 3.
 * Convert struct icaltimetype to struct tm. */
static struct tm
icaltimetype_to_tm (const struct icaltimetype *itt)
{
	struct tm stm;
	memset (&stm, 0, sizeof (stm));

	if (itt == NULL)
		return stm;

	stm.tm_sec = itt->second;
	stm.tm_min = itt->minute;
	stm.tm_hour = itt->hour;
	stm.tm_mday = itt->day;
	stm.tm_mon = itt->month - 1;
	stm.tm_year = itt->year - 1900;
	stm.tm_isdst = -1;

	mktime (&stm);

	return stm;
}

/* Compatibility: tm_to_icaltimetype was removed from libical 3. */
static struct icaltimetype
tm_to_icaltimetype (struct tm *stm, gboolean is_date)
{
	struct icaltimetype itt = icaltime_null_time ();

	if (stm == NULL)
		return itt;

	itt.year = stm->tm_year + 1900;
	itt.month = stm->tm_mon + 1;
	itt.day = stm->tm_mday;
	itt.hour = stm->tm_hour;
	itt.minute = stm->tm_min;
	itt.second = stm->tm_sec;
	itt.is_date = is_date ? 1 : 0;

	return itt;
}

typedef struct _EToDoLocalRecord EToDoLocalRecord;
typedef struct _EToDoConduitCfg EToDoConduitCfg;
typedef struct _EToDoConduitGui EToDoConduitGui;
typedef struct _EToDoConduitContext EToDoConduitContext;

/* Local Record */
struct _EToDoLocalRecord {
	/* The stuff from gnome-pilot-conduit-standard-abs.h
	   Must be first in the structure, or instances of this
	   structure cannot be used by gnome-pilot-conduit-standard-abs.
	*/
	GnomePilotDesktopRecord local;

	/* The corresponding Comp object */
	ECalComponent *comp;

        /* pilot-link todo structure */
	struct ToDo *todo;
};

gint lastDesktopUniqueID;

static void
todoconduit_destroy_record (EToDoLocalRecord *local)
{
	g_object_unref (local->comp);
	free_ToDo (local->todo);
	g_free (local->todo);
	g_free (local);
}

/* Configuration */
struct _EToDoConduitCfg {
	guint32 pilot_id;
	GnomePilotConduitSyncType  sync_type;

	ESourceRegistry *registry;
	ESource *source;
	gboolean secret;
	gint priority;

	gchar *last_uri;
};

static EToDoConduitCfg *
todoconduit_load_configuration (guint32 pilot_id)
{
	EToDoConduitCfg *c;
	GnomePilotConduitManagement *management;
	GnomePilotConduitConfig *config;
	gchar prefix[256];

	g_snprintf (prefix, 255, "e-todo-conduit/Pilot_%u", pilot_id);

	c = g_new0 (EToDoConduitCfg,1);
	g_assert (c != NULL);

	c->pilot_id = pilot_id;

	management = gnome_pilot_conduit_management_new ((gchar *)"e_todo_conduit", GNOME_PILOT_CONDUIT_MGMT_ID);
	g_object_ref_sink (management);
	config = gnome_pilot_conduit_config_new (management, pilot_id);
	g_object_ref_sink (config);
	if (!gnome_pilot_conduit_config_is_enabled (config, &c->sync_type))
		c->sync_type = GnomePilotConduitSyncTypeNotSet;
	g_object_unref (config);
	g_object_unref (management);

	/* Custom settings */
	c->registry = e_source_registry_new_sync (NULL, NULL);
	if (c->registry) {
		c->source = e_pilot_get_sync_source (c->registry, E_SOURCE_EXTENSION_TASK_LIST);
		if (!c->source) {
			GList *sources = e_source_registry_list_sources (c->registry, E_SOURCE_EXTENSION_TASK_LIST);
			if (sources) {
				c->source = g_object_ref (sources->data);
				g_list_free_full (sources, g_object_unref);
			}
		}
		if (c->source) {
			g_object_ref (c->source);
		} else {
			g_object_unref (c->registry);
			c->registry = NULL;
		}
	}

	c->secret = e_pilot_setup_get_bool (prefix, "secret", FALSE);
	c->priority = e_pilot_setup_get_int (prefix, "priority", 3);
	c->last_uri = e_pilot_setup_get_string (prefix, "last_uri", NULL);

	return c;
}

static void
todoconduit_save_configuration (EToDoConduitCfg *c)
{
	gchar prefix[256];

	g_snprintf (prefix, 255, "e-todo-conduit/Pilot_%u", c->pilot_id);

	e_pilot_set_sync_source (c->registry, E_SOURCE_EXTENSION_TASK_LIST, c->source);
	e_pilot_setup_set_bool (prefix, "secret", c->secret);
	e_pilot_setup_set_int (prefix, "priority", c->priority);
	e_pilot_setup_set_string (prefix, "last_uri", c->last_uri ? c->last_uri : "");
}

static EToDoConduitCfg*
todoconduit_dupe_configuration (EToDoConduitCfg *c)
{
	EToDoConduitCfg *retval;

	g_return_val_if_fail (c != NULL, NULL);

	retval = g_new0 (EToDoConduitCfg, 1);
	retval->sync_type = c->sync_type;
	retval->pilot_id = c->pilot_id;

	retval->registry = c->registry ? g_object_ref (c->registry) : NULL;
	if (c->source)
		retval->source = g_object_ref (c->source);
	retval->secret = c->secret;
	retval->priority = c->priority;
	retval->last_uri = g_strdup (c->last_uri);

	return retval;
}

static void
todoconduit_destroy_configuration (EToDoConduitCfg *c)
{
	g_return_if_fail (c != NULL);

	if (c->registry)
		g_object_unref (c->registry);
	if (c->source)
		g_object_unref (c->source);
	g_free (c->last_uri);
	g_free (c);
}

/* Gui */
struct _EToDoConduitGui {
	GtkWidget *priority;
};

static EToDoConduitGui *
e_todo_gui_new (EPilotSettings *ps)
{
	EToDoConduitGui *gui;
	GtkWidget *lbl;
	GtkAdjustment *adj;
	gint rows;

	g_return_val_if_fail (ps != NULL, NULL);
	g_return_val_if_fail (E_IS_PILOT_SETTINGS (ps), NULL);

	/* GtkGrid auto-sizes, no need for gtk_table_resize */

	gui = g_new0 (EToDoConduitGui, 1);

	rows = E_PILOT_SETTINGS_GRID_ROWS;
	lbl = gtk_label_new (_("Default Priority:"));
	gtk_widget_set_halign (lbl, GTK_ALIGN_START);
	gtk_widget_set_valign (lbl, GTK_ALIGN_CENTER);
	adj = gtk_adjustment_new (1, 1, 5, 1, 5, 0);
	gui->priority = gtk_spin_button_new (GTK_ADJUSTMENT (adj), 1.0, 0);
	gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (gui->priority), TRUE);
	gtk_grid_attach (GTK_GRID (ps), lbl, 0, rows, 1, 1);
	gtk_grid_attach (GTK_GRID (ps), gui->priority, 1, rows, 1, 1);

	return gui;
}

static void
e_todo_gui_fill_widgets (EToDoConduitGui *gui, EToDoConduitCfg *cfg)
{
	g_return_if_fail (gui != NULL);
	g_return_if_fail (cfg != NULL);

	gtk_spin_button_set_value (GTK_SPIN_BUTTON (gui->priority), cfg->priority);
}

static void
e_todo_gui_fill_config (EToDoConduitGui *gui, EToDoConduitCfg *cfg)
{
	g_return_if_fail (gui != NULL);
	g_return_if_fail (cfg != NULL);

	cfg->priority = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (gui->priority));
}

static void
e_todo_gui_destroy (EToDoConduitGui *gui)
{
	g_free (gui);
}

/* Context */
struct _EToDoConduitContext {
	GnomePilotDBInfo *dbi;

	EToDoConduitCfg *cfg;
	EToDoConduitCfg *new_cfg;
	EToDoConduitGui *gui;
	GtkWidget *ps;

	struct ToDoAppInfo ai;

	ECalClient *client;

	icaltimezone *timezone;
	ECalComponent *default_comp;
	GList *comps;
	GList *changed;
	GHashTable *changed_hash;
	GList *locals;

	EPilotMap *map;
	gchar *pilot_charset;
};

static EToDoConduitContext *
e_todo_context_new (guint32 pilot_id)
{
	EToDoConduitContext *ctxt = g_new0 (EToDoConduitContext, 1);

	ctxt->cfg = todoconduit_load_configuration (pilot_id);
	ctxt->new_cfg = todoconduit_dupe_configuration (ctxt->cfg);
	ctxt->gui = NULL;
	ctxt->ps = NULL;
	ctxt->client = NULL;
	ctxt->timezone = NULL;
	ctxt->default_comp = NULL;
	ctxt->comps = NULL;
	ctxt->changed_hash = NULL;
	ctxt->changed = NULL;
	ctxt->locals = NULL;
	ctxt->map = NULL;
	ctxt->pilot_charset = NULL;

	return ctxt;
}

static gboolean
e_todo_context_foreach_change (gpointer key, gpointer value, gpointer data)
{
	g_free (key);

	return TRUE;
}

static void
e_todo_context_destroy (EToDoConduitContext *ctxt)
{
	GList *l;

	g_return_if_fail (ctxt != NULL);

	if (ctxt->cfg != NULL)
		todoconduit_destroy_configuration (ctxt->cfg);
	if (ctxt->new_cfg != NULL)
		todoconduit_destroy_configuration (ctxt->new_cfg);
	if (ctxt->gui != NULL)
		e_todo_gui_destroy (ctxt->gui);

	if (ctxt->client != NULL)
		g_object_unref (ctxt->client);

	if (ctxt->default_comp != NULL)
		g_object_unref (ctxt->default_comp);
	if (ctxt->comps != NULL) {
		for (l = ctxt->comps; l; l = l->next)
			g_object_unref (l->data);
		g_list_free (ctxt->comps);
	}

	if (ctxt->changed_hash != NULL) {
		g_hash_table_foreach_remove (ctxt->changed_hash, e_todo_context_foreach_change, NULL);
		g_hash_table_destroy (ctxt->changed_hash);
	}

	if (ctxt->locals != NULL) {
		for (l = ctxt->locals; l != NULL; l = l->next)
			todoconduit_destroy_record (l->data);
		g_list_free (ctxt->locals);
	}

	if (ctxt->changed != NULL)
		e_cal_free_change_list (ctxt->changed);

	if (ctxt->map != NULL)
		e_pilot_map_destroy (ctxt->map);

	g_free (ctxt);
}

/* Debug routines */
static gchar *
print_local (EToDoLocalRecord *local)
{
	static gchar buff[ 4096 ];

	if (local == NULL) {
		sprintf (buff, "[NULL]");
		return buff;
	}

	if (local->todo && local->todo->description) {
		g_snprintf (buff, 4096, "[%d %ld %d %d '%s' '%s' %d]",
			    local->todo->indefinite,
			    mktime (& local->todo->due),
			    local->todo->priority,
			    local->todo->complete,
			    local->todo->description ?
			    local->todo->description : "",
			    local->todo->note ?
			    local->todo->note : "",
			    local->local.category);
		return buff;
	}

	strcpy (buff, "");
	return buff;
}

static gchar *print_remote (GnomePilotRecord *remote, const gchar *pilot_charset)
{
	static gchar buff[ 4096 ];
	struct ToDo todo;
	pi_buffer_t * buffer;
	if (remote == NULL) {
		sprintf (buff, "[NULL]");
		return buff;
	}

	memset (&todo, 0, sizeof (struct ToDo));
	buffer = pi_buffer_new(DLP_BUF_SIZE);
	if (buffer == NULL) {
		sprintf (buff, "[NULL]");
		return buff;
	}
	if (pi_buffer_append(buffer, remote->record, remote->length)==NULL) {
		sprintf (buff, "[NULL]");
		return buff;
	}
	unpack_ToDo (&todo, buffer, todo_v1);
	pi_buffer_free(buffer);
	g_snprintf (buff, 4096, "[%d %ld %d %d '%s' '%s' %d]",
		    todo.indefinite,
		    mktime (&todo.due),
		    todo.priority,
		    todo.complete,
		    todo.description ?
		    e_pilot_utf8_from_pchar(todo.description, pilot_charset) : "",
		    todo.note ?
		    e_pilot_utf8_from_pchar(todo.note, pilot_charset) : "",
		    remote->category);

	free_ToDo (&todo);

	return buff;
}

static gint
start_calendar_server (EToDoConduitContext *ctxt)
{
	GError *error = NULL;

	g_return_val_if_fail (ctxt != NULL, -2);

	if (ctxt->cfg->source) {
		ctxt->client = (ECalClient *) e_cal_client_connect_sync (
			ctxt->cfg->source,
			E_CAL_CLIENT_SOURCE_TYPE_TASKS,
			30, /* timeout seconds */
			NULL, /* cancellable */
			&error);

		if (!ctxt->client) {
			if (error) {
				WARN ("Failed to connect to tasks: %s", error->message);
				g_error_free (error);
			}
			return -1;
		}

		if (ctxt->timezone) {
			ICalTimezone *itz = i_cal_object_construct (
				I_CAL_TYPE_TIMEZONE, ctxt->timezone, NULL, TRUE, NULL);
			e_cal_client_set_default_timezone (ctxt->client, itz);
			g_object_unref (itz);
			LOG (g_message ( "  timezone set to : %s", icaltimezone_get_tzid (ctxt->timezone) ));
		}
	} else {
		return -1;
	}

	return 0;
}

/* Utility routines */
static icaltimezone *
get_timezone (ECalClient *client, const gchar *tzid)
{
	icaltimezone *timezone = NULL;

	timezone = icaltimezone_get_builtin_timezone_from_tzid (tzid);
	if (timezone == NULL) {
		ICalTimezone *izone = NULL;
		if (e_cal_client_get_timezone_sync (client, tzid, &izone, NULL, NULL) && izone)
			timezone = (icaltimezone *) i_cal_object_get_native (I_CAL_OBJECT (izone));
	}

	return timezone;
}

static icaltimezone *
get_default_timezone (void)
{
	icaltimezone *timezone = NULL;
	gchar *location;

	location = e_pilot_setup_get_string ("evolution/calendar/display", "timezone", NULL);

	if (location == NULL || *location == '\0') {
		g_free (location);
		location = g_strdup ("UTC");
	}

	timezone = icaltimezone_get_builtin_timezone (location);
	g_free (location);

	return timezone;
}

static gchar *
map_name (EToDoConduitContext *ctxt)
{
	gchar *basename;
	gchar *filename;

	basename = g_strdup_printf ("pilot-map-todo-%d.xml", ctxt->cfg->pilot_id);

#if EDS_CHECK_VERSION(2,31,6)
	filename = g_build_filename (e_get_user_data_dir (), "tasks", "system", basename, NULL);
#else
	filename = g_build_filename (g_get_home_dir (), ".evolution", "tasks", "local", "system", basename, NULL);
#endif

	g_free (basename);

	return filename;
}

static gboolean
is_empty_time (struct tm time)
{
	if (time.tm_sec || time.tm_min || time.tm_hour
	    || time.tm_mday || time.tm_mon || time.tm_year)
		return FALSE;

	return TRUE;
}

static GList *
next_changed_item (EToDoConduitContext *ctxt, GList *changes)
{
	ECalChange *ccc;
	GList *l;

	for (l = changes; l != NULL; l = l->next) {
		const gchar *uid;

		ccc = l->data;

		uid = e_cal_component_get_uid (ccc->comp);
		if (g_hash_table_lookup (ctxt->changed_hash, uid))
			return l;
	}

	return NULL;
}

static void
compute_status (EToDoConduitContext *ctxt, EToDoLocalRecord *local, const gchar *uid)
{
	ECalChange *ccc;

	local->local.archived = FALSE;
	local->local.secret = FALSE;

	ccc = g_hash_table_lookup (ctxt->changed_hash, uid);

	if (ccc == NULL) {
		local->local.attr = GnomePilotRecordNothing;
		return;
	}

	switch (ccc->type) {
	case E_CAL_CHANGE_ADDED:
		local->local.attr = GnomePilotRecordNew;
		break;
	case E_CAL_CHANGE_MODIFIED:
		local->local.attr = GnomePilotRecordModified;
		break;
	case E_CAL_CHANGE_DELETED:
		local->local.attr = GnomePilotRecordDeleted;
		break;
	}
}

static GnomePilotRecord
local_record_to_pilot_record (EToDoLocalRecord *local,
			      EToDoConduitContext *ctxt)
{
	GnomePilotRecord p;
	pi_buffer_t * buffer;

	g_assert (local->comp != NULL);
	g_assert (local->todo != NULL );

	LOG (g_message ( "local_record_to_pilot_record\n" ));

	memset (&p, 0, sizeof (GnomePilotRecord));

	memset(&p, 0, sizeof (p));

	p.ID = local->local.ID;
	p.category = local->local.category;
	p.attr = local->local.attr;
	p.archived = local->local.archived;
	p.secret = local->local.secret;

	/* Generate pilot record structure */
	buffer = pi_buffer_new(DLP_BUF_SIZE);
	if (buffer == NULL) {
		pi_set_error(ctxt->dbi->pilot_socket, PI_ERR_GENERIC_MEMORY);
		return p;
	}

	pack_ToDo (local->todo, buffer, todo_v1);
	p.record = g_new0(unsigned char, buffer->used);
	p.length = buffer->used;
	memcpy(p.record, buffer->data, buffer->used);

	pi_buffer_free(buffer);
	return p;
}

/*
 * converts a ECalComponent object to a EToDoLocalRecord
 */
static void
local_record_from_comp (EToDoLocalRecord *local, ECalComponent *comp, EToDoConduitContext *ctxt)
{
	const gchar *uid;
	gint priority_val;
	ICalPropertyStatus status;
	ECalComponentText *summary;
	GSList *d_list = NULL;
	ECalComponentText *description;
	ECalComponentDateTime *due;
	ECalComponentClassification classif;
	icaltimezone *default_tz = get_default_timezone ();

	LOG (g_message ( "local_record_from_comp\n" ));

	g_return_if_fail (local != NULL);
	g_return_if_fail (comp != NULL);

	local->comp = comp;
	g_object_ref (comp);

	uid = e_cal_component_get_uid (local->comp);
	local->local.ID = e_pilot_map_lookup_pid (ctxt->map, uid, TRUE);

	compute_status (ctxt, local, uid);

	local->todo = g_new0 (struct ToDo,1);

	/* Don't overwrite the category */
	if (local->local.ID != 0) {
		gint cat = 0;
		pi_buffer_t * record;
		record = pi_buffer_new(DLP_BUF_SIZE);
		if (record == NULL) {
			pi_set_error(ctxt->dbi->pilot_socket, PI_ERR_GENERIC_MEMORY);
			return;
		}

		if (dlp_ReadRecordById (ctxt->dbi->pilot_socket,
					ctxt->dbi->db_handle,
					local->local.ID, record,
					NULL, NULL, &cat) > 0) {
			local->local.category = cat;
		}
		pi_buffer_free(record);
	}

	/*Category support*/
	e_pilot_local_category_to_remote(&(local->local.category), comp, &(ctxt->ai.category), ctxt->pilot_charset);

	/* STOP: don't replace these with g_strdup, since free_ToDo
	   uses free to deallocate */
	summary = e_cal_component_get_summary (comp);
	if (summary && e_cal_component_text_get_value (summary))
		local->todo->description = e_pilot_utf8_to_pchar (e_cal_component_text_get_value (summary), ctxt->pilot_charset);
	if (summary)
		e_cal_component_text_free (summary);

	d_list = e_cal_component_get_descriptions (comp);
	if (d_list) {
		description = (ECalComponentText *) d_list->data;
		if (description && e_cal_component_text_get_value (description))
			local->todo->note = e_pilot_utf8_to_pchar (e_cal_component_text_get_value (description), ctxt->pilot_charset);
		else
			local->todo->note = NULL;
		g_slist_free_full (d_list, e_cal_component_text_free);
	} else {
		local->todo->note = NULL;
	}

	due = e_cal_component_get_due (comp);
	if (due && e_cal_component_datetime_get_value (due)) {
		ICalTime *due_val = e_cal_component_datetime_get_value (due);
		struct icaltimetype *raw_due = (struct icaltimetype *) i_cal_object_get_native (I_CAL_OBJECT (due_val));
		icaltimezone_convert_time (raw_due,
					   get_timezone (ctxt->client, e_cal_component_datetime_get_tzid (due)),
					   default_tz);
		local->todo->due = icaltimetype_to_tm (raw_due);
		local->todo->indefinite = 0;
	} else {
		local->todo->indefinite = 1;
	}
	if (due)
		e_cal_component_datetime_free (due);

	status = e_cal_component_get_status (comp);
	if (status == I_CAL_STATUS_COMPLETED)
		local->todo->complete = 1;
	else
		local->todo->complete = 0;

	priority_val = e_cal_component_get_priority (comp);
	if (priority_val != 0) {
		if (priority_val <= 3)
			local->todo->priority = 1;
		else if (priority_val == 4)
			local->todo->priority = 2;
		else if (priority_val == 5)
			local->todo->priority = 3;
		else if (priority_val <= 7)
			local->todo->priority = 4;
		else
			local->todo->priority = 5;
	} else {
		local->todo->priority = ctxt->cfg->priority;
	}

	classif = e_cal_component_get_classification (comp);

	if (classif == E_CAL_COMPONENT_CLASS_PRIVATE)
		local->local.secret = 1;
	else
		local->local.secret = 0;

	local->local.archived = 0;
}

static void
local_record_from_uid (EToDoLocalRecord *local,
		       const gchar *uid,
		       EToDoConduitContext *ctxt)
{
	ECalComponent *comp;
	ICalComponent *icalcomp;
	GError *error = NULL;

	g_assert(local!=NULL);

	LOG(g_message("local_record_from_uid\n"));

	if (e_cal_client_get_object_sync (ctxt->client, uid, NULL, &icalcomp, NULL, &error)) {
		comp = e_cal_component_new ();
		if (!e_cal_component_set_icalcomponent (comp, icalcomp)) {
			g_object_unref (comp);
			g_object_unref (icalcomp);
			return;
		}

		local_record_from_comp (local, comp, ctxt);
		g_object_unref (comp);
	} else if (g_error_matches (error, E_CAL_CLIENT_ERROR, E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND)) {
		comp = e_cal_component_new ();
		e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_TODO);
		e_cal_component_set_uid (comp, uid);
		local_record_from_comp (local, comp, ctxt);
		g_object_unref (comp);
	} else {
		INFO ("Object did not exist");
	}

	g_clear_error (&error);
}

static ECalComponent *
comp_from_remote_record (GnomePilotConduitSyncAbs *conduit,
			 GnomePilotRecord *remote,
			 ECalComponent *in_comp,
			 icaltimezone *timezone,
			 struct ToDoAppInfo *ai,
			 const gchar *pilot_charset)
{
	ECalComponent *comp;
	struct ToDo todo;
	struct icaltimetype due;
	icaltimezone *utc_zone;
	gint priority;
	gchar *txt;
	pi_buffer_t * buffer;
	ICalTime *now;

	g_return_val_if_fail (remote != NULL, NULL);

	buffer = pi_buffer_new(DLP_BUF_SIZE);
	if (buffer == NULL) {
		return NULL;
	}

	if (pi_buffer_append(buffer, remote->record, remote->length)==NULL) {
		return NULL;
	}

	unpack_ToDo (&todo, buffer, todo_v1);
	pi_buffer_free(buffer);

	utc_zone = icaltimezone_get_utc_timezone ();
	{
		struct icaltimetype raw_now = icaltime_current_time_with_zone (utc_zone);
		now = i_cal_object_construct (I_CAL_TYPE_TIME, &raw_now, NULL, TRUE, NULL);
	}

	if (in_comp == NULL) {
		comp = e_cal_component_new ();
		e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_TODO);
		e_cal_component_set_created (comp, now);
	} else {
		comp = e_cal_component_clone (in_comp);
	}

	e_cal_component_set_last_modified (comp, now);

	{
		ECalComponentText *summary_text;
		txt = e_pilot_utf8_from_pchar (todo.description, pilot_charset);
		summary_text = e_cal_component_text_new (txt, NULL);
		e_cal_component_set_summary (comp, summary_text);
		e_cal_component_text_free (summary_text);
		free (txt);
	}

	/*Category support*/
	e_pilot_remote_category_to_local(remote->category, comp, &(ai->category), pilot_charset);

	/* The iCal description field */
	if (!todo.note) {
		e_cal_component_set_comments (comp, NULL);
	} else {
		GSList l;
		ECalComponentText *text;

		txt = e_pilot_utf8_from_pchar (todo.note, pilot_charset);
		text = e_cal_component_text_new (txt, NULL);
		l.data = text;
		l.next = NULL;

		e_cal_component_set_descriptions (comp, &l);
		e_cal_component_text_free (text);
		free (txt);
	}

	if (todo.complete) {
		e_cal_component_set_completed (comp, now);
		e_cal_component_set_percent_complete (comp, 100);
		e_cal_component_set_status (comp, I_CAL_STATUS_COMPLETED);
	} else {
		gint percent;
		ICalPropertyStatus status;

		e_cal_component_set_completed (comp, NULL);

		percent = e_cal_component_get_percent_complete (comp);
		if (percent == 100 || percent == -1) {
			e_cal_component_set_percent_complete (comp, 0);
		}

		status = e_cal_component_get_status (comp);
		if (status == I_CAL_STATUS_COMPLETED)
			e_cal_component_set_status (comp, I_CAL_STATUS_NEEDSACTION);
	}

	if (!todo.indefinite && !is_empty_time (todo.due)) {
		ECalComponentDateTime *comp_dt;
		ICalTime *ical_due;
		due = tm_to_icaltimetype (&todo.due, TRUE);
		ical_due = i_cal_object_construct (I_CAL_TYPE_TIME, &due, NULL, TRUE, NULL);
		comp_dt = e_cal_component_datetime_new (ical_due, icaltimezone_get_tzid (timezone));
		e_cal_component_set_due (comp, comp_dt);
		e_cal_component_datetime_free (comp_dt);
		g_object_unref (ical_due);
	} else
		e_cal_component_set_due (comp, NULL);

	switch (todo.priority) {
	case 1:
		priority = 3;
		break;
	case 2:
		priority = 5;
		break;
	case 3:
		priority = 5;
		break;
	case 4:
		priority = 7;
		break;
	default:
		priority = 9;
	}

	e_cal_component_set_priority (comp, priority);
	e_cal_component_set_transparency (comp, E_CAL_COMPONENT_TRANSP_NONE);

	if (remote->secret)
		e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_PRIVATE);
	else
		e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_PUBLIC);

	e_cal_component_commit_sequence (comp);

	g_object_unref (now);
	free_ToDo(&todo);

	return comp;
}

static void
check_for_slow_setting (GnomePilotConduit *c, EToDoConduitContext *ctxt)
{
	GnomePilotConduitStandard *conduit = GNOME_PILOT_CONDUIT_STANDARD (c);
	gint map_count;
	const gchar *uri;

	/* If there are no objects or objects but no log */
	map_count = g_hash_table_size (ctxt->map->pid_map);
	if (map_count == 0)
		gnome_pilot_conduit_standard_set_slow (conduit, TRUE);

	/* Or if the URI's don't match */
	{
		ESource *source = e_client_get_source (E_CLIENT (ctxt->client));
		uri = e_source_get_uid (source);
	}
	LOG (g_message ( "  Current URI %s (%s)\n", uri, ctxt->cfg->last_uri ? ctxt->cfg->last_uri : "<NONE>" ));
	if (ctxt->cfg->last_uri != NULL && strcmp (ctxt->cfg->last_uri, uri)) {
		gnome_pilot_conduit_standard_set_slow (conduit, TRUE);
		e_pilot_map_clear (ctxt->map);
	}

	if (gnome_pilot_conduit_standard_get_slow (conduit)) {
		ctxt->map->write_touched_only = TRUE;
		LOG (g_message ( "    doing slow sync\n" ));
	} else {
		LOG (g_message ( "    doing fast sync\n" ));
	}
}

/* Pilot syncing callbacks */
static gint
pre_sync (GnomePilotConduit *conduit,
	  GnomePilotDBInfo *dbi,
	  EToDoConduitContext *ctxt)
{
	GnomePilotConduitSyncAbs *abs_conduit;
	GList *l;
	gint len;
	guchar *buf;
	gchar *filename, *change_id;
	ICalComponent *icalcomp;
	gint num_records, add_records = 0, mod_records = 0, del_records = 0;
	pi_buffer_t * buffer;

	abs_conduit = GNOME_PILOT_CONDUIT_SYNC_ABS (conduit);

	LOG (g_message ( "---------------------------------------------------------\n" ));
	LOG (g_message ( "pre_sync: ToDo Conduit v.%s", CONDUIT_VERSION ));
	g_message ("ToDo Conduit v.%s", CONDUIT_VERSION);

	ctxt->dbi = dbi;
	ctxt->client = NULL;

	if (NULL == dbi->pilotInfo->pilot_charset)
		ctxt->pilot_charset = NULL;
	else
		ctxt->pilot_charset = g_strdup(dbi->pilotInfo->pilot_charset);

	/* Get the timezone */
	ctxt->timezone = get_default_timezone ();
	if (ctxt->timezone == NULL)
		return -1;
	LOG (g_message ( "  Using timezone: %s", icaltimezone_get_tzid (ctxt->timezone) ));

	if (start_calendar_server (ctxt) != 0) {
		WARN(_("Could not start evolution-data-server"));
		gnome_pilot_conduit_error (conduit, _("Could not start evolution-data-server"));
		return -1;
	}

	/* Get the default component */
	if (!e_cal_client_get_default_object_sync (ctxt->client, &icalcomp, NULL, NULL))
		return -1;
	LOG (g_message ("  Got default component: %p", (gpointer) icalcomp));

	ctxt->default_comp = e_cal_component_new ();
	if (!e_cal_component_set_icalcomponent (ctxt->default_comp, icalcomp)) {
		g_object_unref (ctxt->default_comp);
		g_object_unref (icalcomp);
		return -1;
	}

	/* Load the uid <--> pilot id map */
	filename = map_name (ctxt);
	e_pilot_map_read (filename, &ctxt->map);
	g_free (filename);

	/* Get the local database */
	if (!e_cal_client_get_object_list_as_comps_sync (ctxt->client, "#t", &ctxt->comps, NULL, NULL))
		return -1;

	/* Build change list from all components.
	 * NOTE: e_cal_get_changes() was removed in modern EDS.
	 * We treat all existing objects as modified.
	 */
	change_id = g_strdup_printf ("pilot-sync-evolution-todo-%d", ctxt->cfg->pilot_id);
	ctxt->changed = NULL;
	for (l = ctxt->comps; l != NULL; l = l->next) {
		ECalChange *ccc = g_new0 (ECalChange, 1);
		ccc->comp = g_object_ref (l->data);
		ccc->type = E_CAL_CHANGE_MODIFIED;
		ctxt->changed = g_list_prepend (ctxt->changed, ccc);
	}

	ctxt->changed_hash = g_hash_table_new (g_str_hash, g_str_equal);
	g_free (change_id);

	for (l = ctxt->changed; l != NULL; l = l->next) {
		ECalChange *ccc = l->data;
		const gchar *uid;

		uid = e_cal_component_get_uid (ccc->comp);
		if (!e_pilot_map_uid_is_archived (ctxt->map, uid)) {

			g_hash_table_insert (ctxt->changed_hash, g_strdup (uid), ccc);

			switch (ccc->type) {
			case E_CAL_CHANGE_ADDED:
				add_records++;
				break;
			case E_CAL_CHANGE_MODIFIED:
				mod_records++;
				break;
			case E_CAL_CHANGE_DELETED:
				del_records++;
				break;
			}
		} else if (ccc->type == E_CAL_CHANGE_DELETED) {
			e_pilot_map_remove_by_uid (ctxt->map, uid);
		}
	}

	/* Set the count information */
	num_records = g_list_length (ctxt->comps);
	gnome_pilot_conduit_sync_abs_set_num_local_records(abs_conduit, num_records);
	gnome_pilot_conduit_sync_abs_set_num_new_local_records (abs_conduit, add_records);
	gnome_pilot_conduit_sync_abs_set_num_updated_local_records (abs_conduit, mod_records);
	gnome_pilot_conduit_sync_abs_set_num_deleted_local_records(abs_conduit, del_records);

	g_message("num_records: %d\nadd_records: %d\nmod_records: %d\ndel_records: %d\n",
			num_records, add_records, mod_records, del_records);

	buffer = pi_buffer_new(DLP_BUF_SIZE);
	if (buffer == NULL) {
		pi_set_error(dbi->pilot_socket, PI_ERR_GENERIC_MEMORY);
		return -1;
	}
	len = dlp_ReadAppBlock (dbi->pilot_socket, dbi->db_handle, 0,
				DLP_BUF_SIZE,
				buffer);
	if (len < 0) {
		WARN (_("Could not read pilot's ToDo application block"));
		WARN ("dlp_ReadAppBlock(...) = %d", len);
		gnome_pilot_conduit_error (conduit,
					   _("Could not read pilot's ToDo application block"));
		return -1;
	}

	buf = g_new0 (unsigned char,buffer->used);
	memcpy(buf, buffer->data,buffer->used);
	pi_buffer_free(buffer);
	unpack_ToDoAppInfo (&(ctxt->ai), buf, len);
	g_free (buf);

	lastDesktopUniqueID = 128;

	check_for_slow_setting (conduit, ctxt);
	if (ctxt->cfg->sync_type == GnomePilotConduitSyncTypeCopyToPilot
	    || ctxt->cfg->sync_type == GnomePilotConduitSyncTypeCopyFromPilot)
		ctxt->map->write_touched_only = TRUE;

	return 0;
}

static gint
post_sync (GnomePilotConduit *conduit,
	   GnomePilotDBInfo *dbi,
	   EToDoConduitContext *ctxt)
{
	gchar *filename;
	guchar *buf;
	gint dlpRetVal, len;

	buf = (guchar *)g_malloc (0xffff);

	len = pack_ToDoAppInfo (&(ctxt->ai), buf, 0xffff);

	dlpRetVal = dlp_WriteAppBlock (dbi->pilot_socket, dbi->db_handle,
			      (guchar *)buf, len);

	g_free (buf);

	if (dlpRetVal < 0) {
		WARN (_("Could not write pilot's ToDo application block"));
		WARN ("dlp_WriteAppBlock(...) = %d", dlpRetVal);
		gnome_pilot_conduit_error (conduit,
					   _("Could not write pilot's ToDo application block"));
		return -1;
	}

	LOG (g_message ( "post_sync: ToDo Conduit v.%s", CONDUIT_VERSION ));

	g_free (ctxt->cfg->last_uri);
	{
		ESource *source = e_client_get_source (E_CLIENT (ctxt->client));
		ctxt->cfg->last_uri = g_strdup (e_source_get_uid (source));
	}
	todoconduit_save_configuration (ctxt->cfg);

	filename = map_name (ctxt);
	e_pilot_map_write (filename, ctxt->map);
	g_free (filename);
	if (ctxt->pilot_charset)
		g_free (ctxt->pilot_charset);
	LOG (g_message ( "---------------------------------------------------------\n" ));

	return 0;
}

static gint
set_pilot_id (GnomePilotConduitSyncAbs *conduit,
	      EToDoLocalRecord *local,
	      guint32 ID,
	      EToDoConduitContext *ctxt)
{
	const gchar *uid;

	LOG (g_message ( "set_pilot_id: setting to %d\n", ID ));

	uid = e_cal_component_get_uid (local->comp);
	e_pilot_map_insert (ctxt->map, ID, uid, FALSE);

        return 0;
}

static gint
set_status_cleared (GnomePilotConduitSyncAbs *conduit,
		    EToDoLocalRecord *local,
		    EToDoConduitContext *ctxt)
{
	const gchar *uid;

	LOG (g_message ( "set_status_cleared: clearing status\n" ));

	uid = e_cal_component_get_uid (local->comp);
	g_hash_table_remove (ctxt->changed_hash, uid);

        return 0;
}

static gint
for_each (GnomePilotConduitSyncAbs *conduit,
	  EToDoLocalRecord **local,
	  EToDoConduitContext *ctxt)
{
	static GList *comps, *iterator;
	static gint count;
        GList *unused;

	g_return_val_if_fail (local != NULL, -1);

	if (*local == NULL) {
		LOG (g_message ( "beginning for_each" ));

		comps = ctxt->comps;
		count = 0;

		if (comps != NULL) {
			LOG (g_message ( "iterating over %d records", g_list_length (comps)));

			*local = g_new0 (EToDoLocalRecord, 1);
			local_record_from_comp (*local, comps->data, ctxt);

			/* NOTE: ignore the return value, otherwise ctxt->locals
			 * gets messed up. The calling function keeps track of
			 * the *local variable */
			unused = g_list_prepend (ctxt->locals, *local);

			iterator = comps;
		} else {
			LOG (g_message ( "no events" ));
			(*local) = NULL;
			return 0;
		}
	} else {
		count++;
		if (g_list_next (iterator)) {
			iterator = g_list_next (iterator);

			*local = g_new0 (EToDoLocalRecord, 1);
			local_record_from_comp (*local, iterator->data, ctxt);

			/* NOTE: ignore the return value, otherwise ctxt->locals
			 * gets messed up. The calling function keeps track of
			 * the *local variable */
			unused = g_list_prepend (ctxt->locals, *local);
		} else {
			LOG (g_message ( "for_each ending" ));

			/* Tell the pilot the iteration is over */
			*local = NULL;

			return 0;
		}
	}

	return 0;
}

static gint
for_each_modified (GnomePilotConduitSyncAbs *conduit,
		   EToDoLocalRecord **local,
		   EToDoConduitContext *ctxt)
{
	static GList *iterator;
	static gint count;
        GList *unused;

	g_return_val_if_fail (local != NULL, 0);

	if (*local == NULL) {
		LOG (g_message ( "for_each_modified beginning\n" ));

		iterator = ctxt->changed;

		count = 0;

		LOG (g_message ( "iterating over %d records", g_hash_table_size (ctxt->changed_hash) ));

		iterator = next_changed_item (ctxt, iterator);
		if (iterator != NULL) {
			ECalChange *ccc = iterator->data;

			*local = g_new0 (EToDoLocalRecord, 1);
			local_record_from_comp (*local, ccc->comp, ctxt);

			/* NOTE: ignore the return value, otherwise ctxt->locals
			 * gets messed up. The calling function keeps track of
			 * the *local variable */
			unused = g_list_prepend (ctxt->locals, *local);
		} else {
			LOG (g_message ( "no events" ));

			*local = NULL;
		}
	} else {
		count++;
		iterator = g_list_next (iterator);
		if (iterator && (iterator = next_changed_item (ctxt, iterator))) {
			ECalChange *ccc = iterator->data;

			*local = g_new0 (EToDoLocalRecord, 1);
			local_record_from_comp (*local, ccc->comp, ctxt);

			/* NOTE: ignore the return value, otherwise ctxt->locals
			 * gets messed up. The calling function keeps track of
			 * the *local variable */
			unused = g_list_prepend (ctxt->locals, *local);
		} else {
			LOG (g_message ( "for_each_modified ending" ));

			/* Signal the iteration is over */
			*local = NULL;
		}
	}

	return 0;
}

static gint
compare (GnomePilotConduitSyncAbs *conduit,
	 EToDoLocalRecord *local,
	 GnomePilotRecord *remote,
	 EToDoConduitContext *ctxt)
{
	/* used by the quick compare */
	GnomePilotRecord local_pilot;
	gint retval = 0;

	LOG (g_message ("compare: local=%s remote=%s...\n",
			print_local (local), print_remote (remote, ctxt->pilot_charset)));

	g_return_val_if_fail (local!=NULL,-1);
	g_return_val_if_fail (remote!=NULL,-1);

	local_pilot = local_record_to_pilot_record (local, ctxt);

	if (remote->length != local_pilot.length
	    || memcmp (local_pilot.record, remote->record, remote->length))
		retval = 1;

	if (retval == 0)
		LOG (g_message ( "    equal" ));
	else
		LOG (g_message ( "    not equal" ));

	return retval;
}

static gint
add_record (GnomePilotConduitSyncAbs *conduit,
	    GnomePilotRecord *remote,
	    EToDoConduitContext *ctxt)
{
	ECalComponent *comp;
	gchar *uid;
	gint retval = 0;

	g_return_val_if_fail (remote != NULL, -1);

	LOG (g_message ( "add_record: adding %s to desktop\n", print_remote (remote, ctxt->pilot_charset) ));

	comp = comp_from_remote_record (conduit, remote, ctxt->default_comp, ctxt->timezone, &(ctxt->ai), ctxt->pilot_charset);

	/* Give it a new UID otherwise it will be the uid of the default comp */
	uid = e_util_generate_uid ();
	e_cal_component_set_uid (comp, uid);

	if (!e_cal_client_create_object_sync (ctxt->client, e_cal_component_get_icalcomponent (comp), E_CAL_OPERATION_FLAG_NONE, NULL, NULL, NULL))
		return -1;

	e_pilot_map_insert (ctxt->map, remote->ID, uid, FALSE);

	g_object_unref (comp);
	g_free (uid);

	return retval;
}

static gint
replace_record (GnomePilotConduitSyncAbs *conduit,
		EToDoLocalRecord *local,
		GnomePilotRecord *remote,
		EToDoConduitContext *ctxt)
{
	ECalComponent *new_comp;
	gint retval = 0;

	g_return_val_if_fail (remote != NULL, -1);

	LOG (g_message ("replace_record: replace %s with %s\n",
			print_local (local), print_remote (remote, ctxt->pilot_charset)));

	new_comp = comp_from_remote_record (conduit, remote, local->comp, ctxt->timezone, &(ctxt->ai), ctxt->pilot_charset);
	g_object_unref (local->comp);
	local->comp = new_comp;

	if (!e_cal_client_modify_object_sync (ctxt->client, e_cal_component_get_icalcomponent (new_comp),
				       E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE, NULL, NULL))
		return -1;

	return retval;
}

static gint
delete_record (GnomePilotConduitSyncAbs *conduit,
	       EToDoLocalRecord *local,
	       EToDoConduitContext *ctxt)
{
	const gchar *uid;

	g_return_val_if_fail (local != NULL, -1);
	g_return_val_if_fail (local->comp != NULL, -1);

	uid = e_cal_component_get_uid (local->comp);

	LOG (g_message ( "delete_record: deleting %s\n", uid ));

	e_pilot_map_remove_by_uid (ctxt->map, uid);
	/* FIXME Error handling */
	e_cal_client_remove_object_sync (ctxt->client, uid, NULL, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE, NULL, NULL);

        return 0;
}

static gint
archive_record (GnomePilotConduitSyncAbs *conduit,
		EToDoLocalRecord *local,
		gboolean archive,
		EToDoConduitContext *ctxt)
{
	const gchar *uid;
	gint retval = 0;

	g_return_val_if_fail (local != NULL, -1);

	LOG (g_message ( "archive_record: %s\n", archive ? "yes" : "no" ));

	uid = e_cal_component_get_uid (local->comp);
	e_pilot_map_insert (ctxt->map, local->local.ID, uid, archive);

        return retval;
}

static gint
match (GnomePilotConduitSyncAbs *conduit,
       GnomePilotRecord *remote,
       EToDoLocalRecord **local,
       EToDoConduitContext *ctxt)
{
	const gchar *uid;

	LOG (g_message ("match: looking for local copy of %s\n",
			print_remote (remote, ctxt->pilot_charset)));

	g_return_val_if_fail (local != NULL, -1);
	g_return_val_if_fail (remote != NULL, -1);

	*local = NULL;
	uid = e_pilot_map_lookup_uid (ctxt->map, remote->ID, TRUE);

	if (!uid)
		return 0;

	LOG (g_message ( "  matched\n" ));

	*local = g_new0 (EToDoLocalRecord, 1);
	local_record_from_uid (*local, uid, ctxt);

	return 0;
}

static gint
free_match (GnomePilotConduitSyncAbs *conduit,
	    EToDoLocalRecord *local,
	    EToDoConduitContext *ctxt)
{
	LOG (g_message ( "free_match: freeing\n" ));

	g_return_val_if_fail (local != NULL, -1);

	ctxt->locals = g_list_remove (ctxt->locals, local);

	todoconduit_destroy_record (local);

	return 0;
}

static gint
prepare (GnomePilotConduitSyncAbs *conduit,
	 EToDoLocalRecord *local,
	 GnomePilotRecord *remote,
	 EToDoConduitContext *ctxt)
{
	LOG (g_message ( "prepare: encoding local %s\n", print_local (local) ));

	*remote = local_record_to_pilot_record (local, ctxt);

	return 0;
}

/* Pilot Settings Callbacks */
static void
fill_widgets (EToDoConduitContext *ctxt)
{
	if (ctxt->cfg->source)
		e_pilot_settings_set_source (E_PILOT_SETTINGS (ctxt->ps),
					     ctxt->cfg->source);
	e_pilot_settings_set_secret (E_PILOT_SETTINGS (ctxt->ps),
				     ctxt->cfg->secret);

	e_todo_gui_fill_widgets (ctxt->gui, ctxt->cfg);
}

static gint
create_settings_window (GnomePilotConduit *conduit,
			GtkWidget *parent,
			EToDoConduitContext *ctxt)
{
	LOG (g_message ( "create_settings_window" ));

	if (!ctxt->cfg->registry)
		return -1;

	ctxt->ps = e_pilot_settings_new (ctxt->cfg->registry, E_SOURCE_EXTENSION_TASK_LIST);
	ctxt->gui = e_todo_gui_new (E_PILOT_SETTINGS (ctxt->ps));

	if (GTK_IS_BOX (parent))
		gtk_box_append (GTK_BOX (parent), ctxt->ps);
	else
		gtk_widget_set_parent (ctxt->ps, parent);

	fill_widgets (ctxt);

	return 0;
}

static void
display_settings (GnomePilotConduit *conduit, EToDoConduitContext *ctxt)
{
	LOG (g_message ( "display_settings" ));

	fill_widgets (ctxt);
}

static void
save_settings    (GnomePilotConduit *conduit, EToDoConduitContext *ctxt)
{
	LOG (g_message ( "save_settings" ));

	if (ctxt->new_cfg->source)
		g_object_unref (ctxt->new_cfg->source);
	ctxt->new_cfg->source = e_pilot_settings_get_source (E_PILOT_SETTINGS (ctxt->ps));
	g_object_ref (ctxt->new_cfg->source);
	ctxt->new_cfg->secret = e_pilot_settings_get_secret (E_PILOT_SETTINGS (ctxt->ps));
	e_todo_gui_fill_config (ctxt->gui, ctxt->new_cfg);

	todoconduit_save_configuration (ctxt->new_cfg);
}

static void
revert_settings  (GnomePilotConduit *conduit, EToDoConduitContext *ctxt)
{
	LOG (g_message ( "revert_settings" ));

	todoconduit_save_configuration (ctxt->cfg);
	todoconduit_destroy_configuration (ctxt->new_cfg);
	ctxt->new_cfg = todoconduit_dupe_configuration (ctxt->cfg);
}

GnomePilotConduit *
conduit_get_gpilot_conduit (guint32 pilot_id)
{
	GObject *retval;
	EToDoConduitContext *ctxt;

	LOG (g_message ( "in todo's conduit_get_gpilot_conduit\n" ));

	retval = gnome_pilot_conduit_sync_abs_new ((gchar *)"ToDoDB", 0x746F646F);
	g_assert (retval != NULL);

	ctxt = e_todo_context_new (pilot_id);
	g_object_set_data (G_OBJECT (retval), "todoconduit_context", ctxt);

	g_signal_connect (retval, "pre_sync", G_CALLBACK (pre_sync), ctxt);
	g_signal_connect (retval, "post_sync", G_CALLBACK (post_sync), ctxt);

	g_signal_connect (retval, "set_pilot_id", G_CALLBACK (set_pilot_id), ctxt);
	g_signal_connect (retval, "set_status_cleared", G_CALLBACK (set_status_cleared), ctxt);

	g_signal_connect (retval, "for_each", G_CALLBACK (for_each), ctxt);
	g_signal_connect (retval, "for_each_modified", G_CALLBACK (for_each_modified), ctxt);
	g_signal_connect (retval, "compare", G_CALLBACK (compare), ctxt);

	g_signal_connect (retval, "add_record", G_CALLBACK (add_record), ctxt);
	g_signal_connect (retval, "replace_record", G_CALLBACK (replace_record), ctxt);
	g_signal_connect (retval, "delete_record", G_CALLBACK (delete_record), ctxt);
	g_signal_connect (retval, "archive_record", G_CALLBACK (archive_record), ctxt);

	g_signal_connect (retval, "match", G_CALLBACK (match), ctxt);
	g_signal_connect (retval, "free_match", G_CALLBACK (free_match), ctxt);

	g_signal_connect (retval, "prepare", G_CALLBACK (prepare), ctxt);

	/* Gui Settings */
	g_signal_connect (retval, "create_settings_window", G_CALLBACK (create_settings_window), ctxt);
	g_signal_connect (retval, "display_settings", G_CALLBACK (display_settings), ctxt);
	g_signal_connect (retval, "save_settings", G_CALLBACK (save_settings), ctxt);
	g_signal_connect (retval, "revert_settings", G_CALLBACK (revert_settings), ctxt);

	return GNOME_PILOT_CONDUIT (retval);
}

void
conduit_destroy_gpilot_conduit (GnomePilotConduit *conduit)
{
	GObject *obj = G_OBJECT (conduit);
	EToDoConduitContext *ctxt;

	ctxt = g_object_get_data (obj, "todoconduit_context");
	e_todo_context_destroy (ctxt);

	g_object_unref (obj);
}
