/*
 * Evolution calendar - Calendar Conduit
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

#include <config.h>

#include <glib/gi18n.h>
#include <libecal/libecal.h>
#include <libedataserver/libedataserver.h>
#include <pi-source.h>
#include <pi-socket.h>
#include <pi-dlp.h>
#include <pi-datebook.h>
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

#define DEBUG_CALCONDUIT 1
/* #undef DEBUG_CALCONDUIT */

#ifdef DEBUG_CALCONDUIT
#define LOG(x) x
#else
#define LOG(x)
#endif

#define WARN g_warning
#define INFO g_message

#define PILOT_MAX_ADVANCE 99

/* Compatibility: wrap an old icaltimezone* as an ICalTimezone* GObject.
 * The returned object must be freed with g_object_unref().
 * Uses is_global_memory=TRUE so the native icaltimezone is not freed. */
static inline ICalTimezone *
wrap_icaltimezone (icaltimezone *tz)
{
	if (tz == NULL)
		return NULL;
	return (ICalTimezone *) i_cal_object_construct (
		I_CAL_TYPE_TIMEZONE, tz, NULL, TRUE, NULL);
}

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

static struct tm
icaltimetype_to_tm_with_zone (const struct icaltimetype *itt,
			      const icaltimezone *from_zone,
			      const icaltimezone *to_zone)
{
	struct icaltimetype local_itt = *itt;
	icaltimezone_convert_time (&local_itt,
				   (icaltimezone *) from_zone,
				   (icaltimezone *) to_zone);
	return icaltimetype_to_tm (&local_itt);
}

/* Compatibility: tm_to_icaltimetype was removed from libical 3.
 * Convert struct tm to struct icaltimetype. */
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

typedef struct _ECalLocalRecord ECalLocalRecord;
typedef struct _ECalConduitCfg ECalConduitCfg;
typedef struct _ECalConduitGui ECalConduitGui;
typedef struct _ECalConduitContext ECalConduitContext;

/* Local Record */
struct _ECalLocalRecord {
	/* The stuff from gnome-pilot-conduit-standard-abs.h
	   Must be first in the structure, or instances of this
	   structure cannot be used by gnome-pilot-conduit-standard-abs.
	*/
	GnomePilotDesktopRecord local;

	/* The corresponding Comp object */
	ECalComponent *comp;

        /* pilot-link appointment structure */
	struct Appointment *appt;
};

static void
calconduit_destroy_record (ECalLocalRecord *local)
{
	g_object_unref (local->comp);
	free_Appointment (local->appt);
	g_free (local->appt);
	g_free (local);
}

/* Configuration */
struct _ECalConduitCfg {
	guint32 pilot_id;
	GnomePilotConduitSyncType  sync_type;

	ESourceRegistry *registry;
	ESource *source;
	gboolean secret;
	gboolean multi_day_split;

	gchar *last_uri;
};

static ECalConduitCfg *
calconduit_load_configuration (guint32 pilot_id)
{
	ECalConduitCfg *c;
	GnomePilotConduitManagement *management;
	GnomePilotConduitConfig *config;
	gchar prefix[256];

	c = g_new0 (ECalConduitCfg, 1);
	g_assert (c != NULL);

	/* Pilot ID */
	c->pilot_id = pilot_id;

	/* Sync Type */
	management = gnome_pilot_conduit_management_new ((gchar *)"e_calendar_conduit", GNOME_PILOT_CONDUIT_MGMT_ID);
	g_object_ref_sink (management);
	config = gnome_pilot_conduit_config_new (management, pilot_id);
	g_object_ref_sink (config);
	if (!gnome_pilot_conduit_config_is_enabled (config, &c->sync_type))
		c->sync_type = GnomePilotConduitSyncTypeNotSet;
	g_object_unref (config);
	g_object_unref (management);

	/* Custom settings */
	g_snprintf (prefix, 255, "e-calendar-conduit/Pilot_%u", pilot_id);

	c->registry = e_source_registry_new_sync (NULL, NULL);
	if (c->registry) {
		c->source = e_pilot_get_sync_source (c->registry, E_SOURCE_EXTENSION_CALENDAR);
		if (!c->source) {
			GList *sources = e_source_registry_list_sources (c->registry, E_SOURCE_EXTENSION_CALENDAR);
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
	c->multi_day_split = e_pilot_setup_get_bool (prefix, "multi_day_split", TRUE);
	if ((c->last_uri = e_pilot_setup_get_string (prefix, "last_uri", NULL)) && !strncmp (c->last_uri, "file://", 7)) {
		gchar *filename = g_filename_from_uri (c->last_uri, NULL, NULL);
		const gchar *path = filename;
		const gchar *home;

		home = g_get_home_dir ();

		if (!strncmp (path, home, strlen (home))) {
			path += strlen (home);
			if (G_IS_DIR_SEPARATOR (*path))
				path++;

			if (!strcmp (path, "evolution/local/Calendar/calendar.ics")) {
				gchar *new_filename =
#if EDS_CHECK_VERSION(2,31,6)
					g_build_filename (e_get_user_data_dir (), "calendar", "system", "calendar.ics", NULL);
#else
					g_build_filename (home, ".evolution", "calendar", "local", "system", "calendar.ics", NULL);
#endif
				/* need to upgrade the last_uri. yay. */
				g_free (c->last_uri);
				c->last_uri = g_filename_to_uri (new_filename, NULL, NULL);
				g_free (new_filename);
			}
		}
		g_free (filename);
	}

	return c;
}

static void
calconduit_save_configuration (ECalConduitCfg *c)
{
	gchar prefix[256];

	g_snprintf (prefix, 255, "e-calendar-conduit/Pilot_%u", c->pilot_id);

	e_pilot_set_sync_source (c->registry, E_SOURCE_EXTENSION_CALENDAR, c->source);

	e_pilot_setup_set_bool (prefix, "secret", c->secret);
	e_pilot_setup_set_bool (prefix, "multi_day_split", c->multi_day_split);
	e_pilot_setup_set_string (prefix, "last_uri", c->last_uri ? c->last_uri : "");
}

static ECalConduitCfg*
calconduit_dupe_configuration (ECalConduitCfg *c)
{
	ECalConduitCfg *retval;

	g_return_val_if_fail (c != NULL, NULL);

	retval = g_new0 (ECalConduitCfg, 1);
	retval->pilot_id = c->pilot_id;
	retval->sync_type = c->sync_type;

	retval->registry = c->registry ? g_object_ref (c->registry) : NULL;
	if (c->source)
		retval->source = g_object_ref (c->source);
	retval->secret = c->secret;
	retval->multi_day_split = c->multi_day_split;
	retval->last_uri = g_strdup (c->last_uri);

	return retval;
}

static void
calconduit_destroy_configuration (ECalConduitCfg *c)
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
struct _ECalConduitGui {
	GtkWidget *multi_day_split;
};

static ECalConduitGui *
e_cal_gui_new (EPilotSettings *ps)
{
	ECalConduitGui *gui;
	GtkWidget *lbl;
	gint rows;

	g_return_val_if_fail (ps != NULL, NULL);
	g_return_val_if_fail (E_IS_PILOT_SETTINGS (ps), NULL);

	/* GtkGrid auto-sizes, no need for gtk_table_resize */

	gui = g_new0 (ECalConduitGui, 1);

	rows = E_PILOT_SETTINGS_GRID_ROWS;
	lbl = gtk_label_new (_("Split Multi-Day Events:"));
	gui->multi_day_split = gtk_check_button_new ();
	gtk_grid_attach (GTK_GRID (ps), lbl, 0, rows, 1, 1);
	gtk_grid_attach (GTK_GRID (ps), gui->multi_day_split, 1, rows, 1, 1);

	return gui;
}

static void
e_cal_gui_fill_widgets (ECalConduitGui *gui, ECalConduitCfg *cfg)
{
	g_return_if_fail (gui != NULL);
	g_return_if_fail (cfg != NULL);

	gtk_check_button_set_active (GTK_CHECK_BUTTON (gui->multi_day_split),
				      cfg->multi_day_split);
}

static void
e_cal_gui_fill_config (ECalConduitGui *gui, ECalConduitCfg *cfg)
{
	g_return_if_fail (gui != NULL);
	g_return_if_fail (cfg != NULL);

	cfg->multi_day_split = gtk_check_button_get_active (GTK_CHECK_BUTTON (gui->multi_day_split));
}

static void
e_cal_gui_destroy (ECalConduitGui *gui)
{
	g_free (gui);
}

/* Context */
struct _ECalConduitContext {
	GnomePilotDBInfo *dbi;

	ECalConduitCfg *cfg;
	ECalConduitCfg *new_cfg;
	ECalConduitGui *gui;
	GtkWidget *ps;

	struct AppointmentAppInfo ai;

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

static ECalConduitContext *
e_calendar_context_new (guint32 pilot_id)
{
	ECalConduitContext *ctxt;

	ctxt = g_new0 (ECalConduitContext, 1);
	g_assert (ctxt != NULL);

	ctxt->cfg = calconduit_load_configuration (pilot_id);
	ctxt->new_cfg = calconduit_dupe_configuration (ctxt->cfg);
	ctxt->ps = NULL;
	ctxt->dbi = NULL;
	ctxt->client = NULL;
	ctxt->timezone = NULL;
	ctxt->default_comp = NULL;
	ctxt->comps = NULL;
	ctxt->changed = NULL;
	ctxt->changed_hash = NULL;
	ctxt->locals = NULL;
	ctxt->map = NULL;

	return ctxt;
}

static gboolean
e_calendar_context_foreach_change (gpointer key, gpointer value, gpointer data)
{
	g_free (key);

	return TRUE;
}

static void
e_calendar_context_destroy (ECalConduitContext *ctxt)
{
	GList *l;

	g_return_if_fail (ctxt != NULL);

	if (ctxt->cfg != NULL)
		calconduit_destroy_configuration (ctxt->cfg);
	if (ctxt->new_cfg != NULL)
		calconduit_destroy_configuration (ctxt->new_cfg);
	if (ctxt->gui != NULL)
		e_cal_gui_destroy (ctxt->gui);

	if (ctxt->client != NULL)
		g_object_unref (ctxt->client);
	if (ctxt->default_comp != NULL)
		g_object_unref (ctxt->default_comp);
	if (ctxt->comps != NULL) {
		for (l = ctxt->comps; l; l = l->next)
			g_object_unref (l->data);
		g_list_free (ctxt->comps);
	}

	if (ctxt->changed != NULL)
		e_cal_free_change_list (ctxt->changed);

	if (ctxt->changed_hash != NULL) {
		g_hash_table_foreach_remove (ctxt->changed_hash, e_calendar_context_foreach_change, NULL);
		g_hash_table_destroy (ctxt->changed_hash);
	}

	if (ctxt->locals != NULL) {
		for (l = ctxt->locals; l != NULL; l = l->next)
			calconduit_destroy_record (l->data);
		g_list_free (ctxt->locals);
	}

	if (ctxt->map != NULL)
		e_pilot_map_destroy (ctxt->map);
}

/* Debug routines */
static gchar *
print_local (ECalLocalRecord *local)
{
	static gchar buff[ 4096 ];

	if (local == NULL) {
		sprintf (buff, "[NULL]");
		return buff;
	}

	if (local->appt && local->appt->description) {
		g_snprintf (buff, 4096, "[%ld %ld '%s' '%s']",
			    mktime (&local->appt->begin),
			    mktime (&local->appt->end),
			    local->appt->description ?
			    local->appt->description : "",
			    local->appt->note ?
			    local->appt->note : "");
		return buff;
	}

	strcpy (buff, "");
	return buff;
}

static gchar *print_remote (GnomePilotRecord *remote)
{
	static gchar buff[ 4096 ];
	struct Appointment appt;
	pi_buffer_t * buffer;

	if (remote == NULL) {
		sprintf (buff, "[NULL]");
		return buff;
	}

	memset (&appt, 0, sizeof (struct Appointment));
	buffer = pi_buffer_new(DLP_BUF_SIZE);
	if (buffer == NULL) {
		sprintf (buff, "[NULL]");
		return buff;
	}
	if (pi_buffer_append(buffer, remote->record, remote->length)==NULL) {
		sprintf (buff, "[NULL]");
		return buff;
	}

	unpack_Appointment (&appt, buffer, datebook_v1);
	pi_buffer_free(buffer);
	g_snprintf (buff, 4096, "[%ld %ld '%s' '%s']",
		    mktime (&appt.begin),
		    mktime (&appt.end),
		    appt.description ?
		    appt.description : "",
		    appt.note ?
		    appt.note : "");

	free_Appointment (&appt);

	return buff;
}

static gint
start_calendar_server (ECalConduitContext *ctxt)
{
	GError *error = NULL;

	g_return_val_if_fail (ctxt != NULL, -2);

	if (ctxt->cfg->source) {
		ctxt->client = (ECalClient *) e_cal_client_connect_sync (
			ctxt->cfg->source,
			E_CAL_CLIENT_SOURCE_TYPE_EVENTS,
			30, /* timeout seconds */
			NULL, /* cancellable */
			&error);

		if (!ctxt->client) {
			if (error) {
				WARN ("Failed to connect to calendar: %s", error->message);
				g_error_free (error);
			}
			return -1;
		}

		if (ctxt->timezone) {
			ICalTimezone *itz = wrap_icaltimezone (ctxt->timezone);
			e_cal_client_set_default_timezone (ctxt->client, itz);
			g_object_unref (itz);
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
map_name (ECalConduitContext *ctxt)
{
	gchar *basename;
	gchar *filename;

	basename = g_strdup_printf ("pilot-map-calendar-%d.xml", ctxt->cfg->pilot_id);

#if EDS_CHECK_VERSION(2,31,6)
	filename = g_build_filename (e_get_user_data_dir (), "calendar", "system", basename, NULL);
#else
	filename = g_build_filename (g_get_home_dir (), ".evolution", "calendar", "local", "system", basename, NULL);
#endif

	g_free (basename);

	return filename;
}

static icalrecurrencetype_weekday
get_ical_day (gint day)
{
	switch (day) {
	case 0:
		return ICAL_SUNDAY_WEEKDAY;
	case 1:
		return ICAL_MONDAY_WEEKDAY;
	case 2:
		return ICAL_TUESDAY_WEEKDAY;
	case 3:
		return ICAL_WEDNESDAY_WEEKDAY;
	case 4:
		return ICAL_THURSDAY_WEEKDAY;
	case 5:
		return ICAL_FRIDAY_WEEKDAY;
	case 6:
		return ICAL_SATURDAY_WEEKDAY;
	}

	return ICAL_NO_WEEKDAY;
}

static gint
get_pilot_day (icalrecurrencetype_weekday wd)
{
	switch (wd) {
	case ICAL_SUNDAY_WEEKDAY:
		return 0;
	case ICAL_MONDAY_WEEKDAY:
		return 1;
	case ICAL_TUESDAY_WEEKDAY:
		return 2;
	case ICAL_WEDNESDAY_WEEKDAY:
		return 3;
	case ICAL_THURSDAY_WEEKDAY:
		return 4;
	case ICAL_FRIDAY_WEEKDAY:
		return 5;
	case ICAL_SATURDAY_WEEKDAY:
		return 6;
	default:
		return -1;
	}
}

static gboolean
is_empty_time (struct tm time)
{
	if (time.tm_sec || time.tm_min || time.tm_hour
	    || time.tm_mday || time.tm_mon || time.tm_year)
		return FALSE;

	return TRUE;
}

static gboolean
is_all_day (ECalClient *client, ECalComponentDateTime *dt_start, ECalComponentDateTime *dt_end)
{
	time_t dt_start_time, dt_end_time;
	icaltimezone *timezone;
	ICalTime *start_val = e_cal_component_datetime_get_value (dt_start);
	ICalTime *end_val = e_cal_component_datetime_get_value (dt_end);
	const gchar *start_tzid = e_cal_component_datetime_get_tzid (dt_start);
	const gchar *end_tzid = e_cal_component_datetime_get_tzid (dt_end);

	if (i_cal_time_is_date (start_val) && i_cal_time_is_date (end_val))
		return TRUE;

	timezone = get_timezone (client, start_tzid);
	dt_start_time = icaltime_as_timet_with_zone (*(struct icaltimetype *) i_cal_object_get_native (I_CAL_OBJECT (start_val)), timezone);
	dt_end_time = icaltime_as_timet_with_zone (*(struct icaltimetype *) i_cal_object_get_native (I_CAL_OBJECT (end_val)), get_timezone (client, end_tzid));

	{
		ICalTimezone *itz = wrap_icaltimezone (timezone);
		gboolean result = (dt_end_time == time_add_day_with_zone (dt_start_time, 1, itz));
		g_object_unref (itz);
		if (result)
			return TRUE;
	}

	return FALSE;
}

static gboolean
process_multi_day (ECalConduitContext *ctxt, ECalChange *ccc, GList **multi_comp, GList **multi_ccc)
{
	ECalComponentDateTime *dt_start, *dt_end;
	ICalTime *start_val, *end_val;
	icaltimezone *tz_start, *tz_end;
	time_t event_start, event_end, day_end;
	const gchar *uid;
	gboolean is_date = FALSE;
	gboolean last = FALSE;
	gboolean ret = TRUE;

	*multi_ccc = NULL;
	*multi_comp = NULL;

	if (ccc->type == E_CAL_CHANGE_DELETED)
		return FALSE;

	/* Start time */
	dt_start = e_cal_component_get_dtstart (ccc->comp);
	start_val = dt_start ? e_cal_component_datetime_get_value (dt_start) : NULL;
	if (!start_val) {
		if (dt_start) e_cal_component_datetime_free (dt_start);
		return FALSE;
	}
	if (i_cal_time_is_date (start_val))
		tz_start = ctxt->timezone;
	else
		tz_start = get_timezone (ctxt->client, e_cal_component_datetime_get_tzid (dt_start));
	event_start = icaltime_as_timet_with_zone (*(struct icaltimetype *) i_cal_object_get_native (I_CAL_OBJECT (start_val)), tz_start);

	dt_end = e_cal_component_get_dtend (ccc->comp);
	end_val = dt_end ? e_cal_component_datetime_get_value (dt_end) : NULL;
	if (!end_val) {
		e_cal_component_datetime_free (dt_start);
		if (dt_end) e_cal_component_datetime_free (dt_end);
		return FALSE;
	}
	if (i_cal_time_is_date (end_val))
		tz_end = ctxt->timezone;
	else
		tz_end = get_timezone (ctxt->client, e_cal_component_datetime_get_tzid (dt_end));
	event_end = icaltime_as_timet_with_zone (*(struct icaltimetype *) i_cal_object_get_native (I_CAL_OBJECT (end_val)), tz_end);

	{
		ICalTimezone *itz = wrap_icaltimezone (ctxt->timezone);
		day_end = time_day_end_with_zone (event_start, itz);
		g_object_unref (itz);
	}
	if (day_end >= event_end) {
		ret = FALSE;
		goto cleanup;
	} else if (e_cal_component_has_recurrences (ccc->comp) || !ctxt->cfg->multi_day_split) {
		ret = TRUE;
		goto cleanup;
	}

	if (i_cal_time_is_date (start_val) && i_cal_time_is_date (end_val))
		is_date = TRUE;

	while (!last) {
		ECalComponent *clone = e_cal_component_clone (ccc->comp);
		ICalComponent *ical_comp = NULL;
		gchar *new_uid = e_util_generate_uid ();
		ICalTime *new_start, *new_end;
		ECalComponentDateTime *new_dt_start, *new_dt_end;
		ECalChange *c = NULL;

		if (day_end >= event_end) {
			day_end = event_end;
			last = TRUE;
		}

		e_cal_component_set_uid (clone, new_uid);

		{
			struct icaltimetype raw_start = icaltime_from_timet_with_zone (event_start, is_date, tz_start);
			new_start = i_cal_object_construct (I_CAL_TYPE_TIME, &raw_start, NULL, TRUE, NULL);
		}
		new_dt_start = e_cal_component_datetime_new (new_start, e_cal_component_datetime_get_tzid (dt_start));
		e_cal_component_set_dtstart (clone, new_dt_start);
		e_cal_component_datetime_free (new_dt_start);
		g_object_unref (new_start);

		{
			struct icaltimetype raw_end = icaltime_from_timet_with_zone (day_end, is_date, tz_end);
			new_end = i_cal_object_construct (I_CAL_TYPE_TIME, &raw_end, NULL, TRUE, NULL);
		}
		new_dt_end = e_cal_component_datetime_new (new_end, e_cal_component_datetime_get_tzid (dt_end));
		e_cal_component_set_dtend (clone, new_dt_end);
		e_cal_component_datetime_free (new_dt_end);
		g_object_unref (new_end);

		e_cal_component_commit_sequence (clone);

		/* FIXME Error handling */
		ical_comp = e_cal_component_get_icalcomponent (clone);
		if (!ical_comp) {
			ret = FALSE;
			g_free (new_uid);
			g_object_unref (clone);
			goto cleanup;
		}

		e_cal_client_create_object_sync (ctxt->client, ical_comp, E_CAL_OPERATION_FLAG_NONE, NULL, NULL, NULL);

		c = g_new0 (ECalChange, 1);
		c->comp = clone;
		c->type = E_CAL_CHANGE_ADDED;

		*multi_ccc = g_list_prepend (*multi_ccc, c);
		*multi_comp = g_list_prepend (*multi_comp, g_object_ref (c->comp));

		event_start = day_end;
		{
		ICalTimezone *itz = wrap_icaltimezone (ctxt->timezone);
		day_end = time_day_end_with_zone (event_start, itz);
		g_object_unref (itz);
	}

		g_free (new_uid);
	}

	uid = e_cal_component_get_uid (ccc->comp);
	/* FIXME Error handling */
	e_cal_client_remove_object_sync (ctxt->client, uid, NULL, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE, NULL, NULL);

	ccc->type = E_CAL_CHANGE_DELETED;

 cleanup:
	e_cal_component_datetime_free (dt_start);
	e_cal_component_datetime_free (dt_end);

	return ret;
}

static short
nth_weekday (gint pos, icalrecurrencetype_weekday weekday)
{
	g_assert ((pos > 0 && pos <= 5) || (pos == -1));

	return ((abs (pos) * 8) + weekday) * (pos < 0 ? -1 : 1);
}

static GList *
next_changed_item (ECalConduitContext *ctxt, GList *changes)
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
compute_status (ECalConduitContext *ctxt, ECalLocalRecord *local, const gchar *uid)
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

static gboolean
rrules_mostly_equal (struct icalrecurrencetype *a, struct icalrecurrencetype *b)
{
	struct icalrecurrencetype acopy, bcopy;

	acopy = *a;
	bcopy = *b;

	acopy.until = bcopy.until = icaltime_null_time ();
	acopy.count = bcopy.count = 0;

	if (!memcmp (&acopy, &bcopy, sizeof (struct icalrecurrencetype)))
		return TRUE;

	return FALSE;
}

/* Timezone resolver callback for e_cal_recur_generate_instances_sync.
 * user_data is the ECalClient *. */
static ICalTimezone *
resolve_tzid_cb (const gchar *tzid, gpointer user_data, GCancellable *cancellable, GError **error)
{
	ECalClient *client = E_CAL_CLIENT (user_data);
	ICalTimezone *zone = NULL;

	if (tzid == NULL || *tzid == '\0')
		return NULL;

	e_cal_client_get_timezone_sync (client, tzid, &zone, cancellable, error);
	return zone;
}

static gboolean
find_last_cb (ICalComponent *icomp, ICalTime *instance_start, ICalTime *instance_end, gpointer data, GCancellable *cancellable, GError **error)
{
	time_t *last = data;

	*last = i_cal_time_as_timet (instance_start);

	return TRUE;
}

static GnomePilotRecord
local_record_to_pilot_record (ECalLocalRecord *local,
			      ECalConduitContext *ctxt)
{
	GnomePilotRecord p;
	pi_buffer_t * buffer;

	memset(&p, 0, sizeof (p));

	g_assert (local->comp != NULL);
	g_assert (local->appt != NULL );

	memset (&p, 0, sizeof (GnomePilotRecord));

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

	pack_Appointment (local->appt, buffer, datebook_v1);
	p.record = g_new0(unsigned char, buffer->used);
	p.length = buffer->used;
	memcpy(p.record, buffer->data, buffer->used);

	pi_buffer_free(buffer);
	return p;
}

/*
 * converts a ECalComponent object to a ECalLocalRecord
 */
static void
local_record_from_comp (ECalLocalRecord *local, ECalComponent *comp, ECalConduitContext *ctxt)
{
	const gchar *uid;
	ECalComponentText *summary;
	GSList *d_list = NULL, *edl = NULL, *l;
	ECalComponentText *description;
	ECalComponentDateTime *dt_start, *dt_end;
	ECalComponentClassification classif;
	icaltimezone *default_tz = ctxt->timezone;
	gint i;

	g_return_if_fail (local != NULL);
	g_return_if_fail (comp != NULL);

	local->comp = comp;
	g_object_ref (comp);

	uid = e_cal_component_get_uid (local->comp);
	local->local.ID = e_pilot_map_lookup_pid (ctxt->map, uid, TRUE);
	compute_status (ctxt, local, uid);

	local->appt = g_new0 (struct Appointment, 1);

	/* Handle the fields and category we don't sync by making sure
         * we don't overwrite them
	 */
	if (local->local.ID != 0) {
		gint cat = 0;
		struct Appointment appt;
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
			memset (&appt, 0, sizeof (struct Appointment));
			unpack_Appointment (&appt, record, datebook_v1);
			local->appt->alarm = appt.alarm;
			local->appt->advance = appt.advance;
			local->appt->advanceUnits = appt.advanceUnits;
			free_Appointment (&appt);
		}
		pi_buffer_free (record);
	}

	/*Category support*/
	e_pilot_local_category_to_remote(&(local->local.category), comp, &(ctxt->ai.category), ctxt->pilot_charset);

	/* STOP: don't replace these with g_strdup, since free_Appointment
	   uses free to deallocate */
	summary = e_cal_component_get_summary (comp);
	if (summary && e_cal_component_text_get_value (summary))
		local->appt->description = e_pilot_utf8_to_pchar (e_cal_component_text_get_value (summary), ctxt->pilot_charset);
	if (summary)
		e_cal_component_text_free (summary);

	d_list = e_cal_component_get_descriptions (comp);
	if (d_list) {
		description = (ECalComponentText *) d_list->data;
		if (description && e_cal_component_text_get_value (description))
			local->appt->note = e_pilot_utf8_to_pchar (e_cal_component_text_get_value (description), ctxt->pilot_charset);
		else
			local->appt->note = NULL;
		g_slist_free_full (d_list, e_cal_component_text_free);
	} else {
		local->appt->note = NULL;
	}

	/* Start/End */
	dt_start = e_cal_component_get_dtstart (comp);
	dt_end = e_cal_component_get_dtend (comp);
	if (dt_start && e_cal_component_datetime_get_value (dt_start)) {
		ICalTime *start_val = e_cal_component_datetime_get_value (dt_start);
		struct icaltimetype *native_start = (struct icaltimetype *) i_cal_object_get_native (I_CAL_OBJECT (start_val));
		icaltimezone_convert_time (native_start,
					   get_timezone (ctxt->client, e_cal_component_datetime_get_tzid (dt_start)),
					   default_tz);
		local->appt->begin = icaltimetype_to_tm (native_start);
	}

	if (dt_start && e_cal_component_datetime_get_value (dt_start) &&
	    dt_end && e_cal_component_datetime_get_value (dt_end)) {
		if (is_all_day (ctxt->client, dt_start, dt_end)) {
			local->appt->event = 1;
		} else {
			ICalTime *end_val = e_cal_component_datetime_get_value (dt_end);
			struct icaltimetype *native_end = (struct icaltimetype *) i_cal_object_get_native (I_CAL_OBJECT (end_val));
			icaltimezone_convert_time (native_end,
						   get_timezone (ctxt->client, e_cal_component_datetime_get_tzid (dt_end)),
						   default_tz);
			local->appt->end = icaltimetype_to_tm (native_end);
			local->appt->event = 0;
		}
	} else {
		local->appt->event = 1;
	}
	if (dt_start) e_cal_component_datetime_free (dt_start);
	if (dt_end) e_cal_component_datetime_free (dt_end);

	/* Recurrence Rules */
	local->appt->repeatType = repeatNone;

	if (!e_cal_component_is_instance (comp)) {
		if (e_cal_component_has_rrules (comp)) {
			GSList *list;
			ICalRecurrence *irecur;
			struct icalrecurrencetype *recur;

			list = e_cal_component_get_rrules (comp);
			irecur = list->data;
			recur = (struct icalrecurrencetype *) i_cal_object_get_native (I_CAL_OBJECT (irecur));

			switch (recur->freq) {
			case ICAL_DAILY_RECURRENCE:
				local->appt->repeatType = repeatDaily;
				break;
			case ICAL_WEEKLY_RECURRENCE:
				local->appt->repeatType = repeatWeekly;
				for (i = 0; i <= 7 && recur->by_day[i] != ICAL_RECURRENCE_ARRAY_MAX; i++) {
					icalrecurrencetype_weekday wd;

					wd = icalrecurrencetype_day_day_of_week (recur->by_day[i]);
					local->appt->repeatDays[get_pilot_day (wd)] = 1;
				}

				break;
			case ICAL_MONTHLY_RECURRENCE:
				if (recur->by_month_day[0] != ICAL_RECURRENCE_ARRAY_MAX) {
					local->appt->repeatType = repeatMonthlyByDate;
					break;
				}

				/* Not going to work with -ve  by_day/by_set_pos other than -1,
				 * pilot doesn't support that anyhow */
				local->appt->repeatType = repeatMonthlyByDay;
				switch (recur->by_set_pos[0] != ICAL_RECURRENCE_ARRAY_MAX ? recur->by_set_pos[0]
					: icalrecurrencetype_day_position (recur->by_day[0])) {
				case 1:
					local->appt->repeatDay = dom1stSun;
					break;
				case 2:
					local->appt->repeatDay = dom2ndSun;
					break;
				case 3:
					local->appt->repeatDay = dom3rdSun;
					break;
				case 4:
					local->appt->repeatDay = dom4thSun;
					break;
				case -1:
				case 5:
					local->appt->repeatDay = domLastSun;
					break;
				}
				local->appt->repeatDay += get_pilot_day (icalrecurrencetype_day_day_of_week (recur->by_day[0]));
				break;
			case ICAL_YEARLY_RECURRENCE:
				local->appt->repeatType = repeatYearly;
				break;
			default:
				break;
			}

			if (local->appt->repeatType != repeatNone) {
				local->appt->repeatFrequency = recur->interval;
			}

			if (!icaltime_is_null_time (recur->until)) {
				local->appt->repeatForever = 0;
				local->appt->repeatEnd = icaltimetype_to_tm_with_zone (&recur->until,
										       icaltimezone_get_utc_timezone (),
										       default_tz);
			} else if (recur->count > 0) {
				time_t last = -1;
				struct icaltimetype itt;

				/* The palm does not support count recurrences */
				local->appt->repeatForever = 0;
				{
					ICalTimezone *itz = i_cal_object_construct (
						I_CAL_TYPE_TIMEZONE, default_tz, NULL, TRUE, NULL);
					e_cal_recur_generate_instances_sync (
						e_cal_component_get_icalcomponent (comp),
						NULL, NULL,
						find_last_cb, &last,
						resolve_tzid_cb, ctxt->client,
						itz, NULL, NULL);
					g_object_unref (itz);
				}
				itt = icaltime_from_timet_with_zone (last, TRUE, default_tz);
				local->appt->repeatEnd = icaltimetype_to_tm (&itt);
			} else {
				local->appt->repeatForever = 1;
			}

			g_slist_free_full (list, g_object_unref);
		}

		/* Exceptions */
		edl = e_cal_component_get_exdates (comp);
		local->appt->exceptions = g_slist_length (edl);
		local->appt->exception = g_new0 (struct tm, local->appt->exceptions);
		for (l = edl, i = 0; l != NULL; l = l->next, i++) {
			ECalComponentDateTime *dt = l->data;
			ICalTime *dt_val = e_cal_component_datetime_get_value (dt);
			struct icaltimetype *native_dt = (struct icaltimetype *) i_cal_object_get_native (I_CAL_OBJECT (dt_val));

			icaltimezone_convert_time (native_dt,
						   icaltimezone_get_utc_timezone (),
						   default_tz);
			local->appt->exception[i] = icaltimetype_to_tm (native_dt);
		}
		g_slist_free_full (edl, e_cal_component_datetime_free);
	}

	/* Alarm */
	local->appt->alarm = 0;
	if (e_cal_component_has_alarms (comp)) {
		GSList *uids, *sl;
		ECalComponentAlarm *alarm;
		ECalComponentAlarmTrigger *trigger;

		uids = e_cal_component_get_alarm_uids (comp);
		for (sl = uids; sl != NULL; sl = sl->next) {
			alarm = e_cal_component_get_alarm (comp, sl->data);
			trigger = e_cal_component_alarm_get_trigger (alarm);

			if (trigger && e_cal_component_alarm_trigger_get_kind (trigger) == E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START) {
				ICalDuration *dur = e_cal_component_alarm_trigger_get_duration (trigger);
				if (dur && i_cal_duration_is_neg (dur)) {
					gint minutes = i_cal_duration_get_minutes (dur);
					gint hours = i_cal_duration_get_hours (dur);
					gint days = i_cal_duration_get_days (dur);
					gint weeks = i_cal_duration_get_weeks (dur);

					local->appt->advanceUnits = advMinutes;
					local->appt->advance =
						minutes + hours * 60
						+ days * 60 * 24
						+ weeks * 7 * 60 * 24;

					if (local->appt->advance > PILOT_MAX_ADVANCE) {
						local->appt->advanceUnits = advHours;
						local->appt->advance =
							minutes / 60 + hours
							+ days * 24
							+ weeks * 7 * 24;
					}
					if (local->appt->advance > PILOT_MAX_ADVANCE) {
						local->appt->advanceUnits = advDays;
						local->appt->advance =
							minutes / (60 * 24)
							+ hours / 24
							+ days
							+ weeks * 7;
					}
					if (local->appt->advance > PILOT_MAX_ADVANCE)
						local->appt->advance = PILOT_MAX_ADVANCE;

					local->appt->alarm = 1;
					e_cal_component_alarm_free (alarm);
					break;
				} else if (dur && i_cal_duration_as_int (dur) == 0) {
					local->appt->advanceUnits = advMinutes;
					local->appt->advance = 0;
					local->appt->alarm = 1;
					e_cal_component_alarm_free (alarm);
					break;
				}
			}
			e_cal_component_alarm_free (alarm);
		}
		g_slist_free_full (uids, g_free);
	}

	classif = e_cal_component_get_classification (comp);

	if (classif == E_CAL_COMPONENT_CLASS_PRIVATE)
		local->local.secret = 1;
	else
		local->local.secret = 0;

	local->local.archived = 0;
}

static void
local_record_from_uid (ECalLocalRecord *local,
		       const gchar *uid,
		       ECalConduitContext *ctxt)
{
	ECalComponent *comp;
	ICalComponent *icalcomp;
	GError *error = NULL;

	g_assert(local!=NULL);

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
		e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);
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
			 ECalClient *client,
			 icaltimezone *timezone,
			 struct CategoryAppInfo *category,
			 const gchar *pilot_charset)
{
	ECalComponent *comp;
	struct Appointment appt;
	ICalTime *now;
	struct icaltimetype it;
	struct icalrecurrencetype recur;
	ECalComponentText *summary_text;
	GSList *edl = NULL;
	gchar *txt;
	gint pos, i;
	pi_buffer_t * buffer;
	g_return_val_if_fail (remote != NULL, NULL);

	buffer = pi_buffer_new(DLP_BUF_SIZE);
	if (buffer == NULL) {
		return NULL;
	}

	if (pi_buffer_append(buffer, remote->record, remote->length)==NULL) {
		return NULL;
	}

	unpack_Appointment (&appt, buffer, datebook_v1);
	pi_buffer_free(buffer);

	{
		struct icaltimetype raw_now = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
		now = i_cal_object_construct (I_CAL_TYPE_TIME, &raw_now, NULL, TRUE, NULL);
	}

	if (in_comp == NULL) {
		comp = e_cal_component_new ();
		e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);
		e_cal_component_set_created (comp, now);
	} else {
		comp = e_cal_component_clone (in_comp);
	}

	e_cal_component_set_last_modified (comp, now);
	g_object_unref (now);

	txt = e_pilot_utf8_from_pchar (appt.description, pilot_charset);
	summary_text = e_cal_component_text_new (txt, NULL);
	e_cal_component_set_summary (comp, summary_text);
	e_cal_component_text_free (summary_text);
	free (txt);

	/*Category support*/
	e_pilot_remote_category_to_local(remote->category, comp, category, pilot_charset);

	/* The iCal description field */
	if (!appt.note) {
		e_cal_component_set_descriptions (comp, NULL);
	} else {
		GSList l;
		ECalComponentText *text;

		txt = e_pilot_utf8_from_pchar (appt.note, pilot_charset);
		text = e_cal_component_text_new (txt, NULL);
		l.data = text;
		l.next = NULL;

		e_cal_component_set_descriptions (comp, &l);
		e_cal_component_text_free (text);
		free (txt);
	}

	if (appt.event && !is_empty_time (appt.begin)) {
		ICalTime *ical_it;
		ECalComponentDateTime *edt;
		it = tm_to_icaltimetype (&appt.begin, TRUE);
		ical_it = i_cal_object_construct (I_CAL_TYPE_TIME, &it, NULL, TRUE, NULL);
		edt = e_cal_component_datetime_new (ical_it, NULL);
		e_cal_component_set_dtstart (comp, edt);
		e_cal_component_set_dtend (comp, edt);
		e_cal_component_datetime_free (edt);
		g_object_unref (ical_it);
	} else {
		const gchar *tzid_str = icaltimezone_get_tzid (timezone);

		if (!is_empty_time (appt.begin)) {
			ICalTime *ical_it;
			ECalComponentDateTime *edt;
			it = tm_to_icaltimetype (&appt.begin, FALSE);
			ical_it = i_cal_object_construct (I_CAL_TYPE_TIME, &it, NULL, TRUE, NULL);
			edt = e_cal_component_datetime_new (ical_it, tzid_str);
			e_cal_component_set_dtstart (comp, edt);
			e_cal_component_datetime_free (edt);
			g_object_unref (ical_it);
		}

		if (!is_empty_time (appt.end)) {
			ICalTime *ical_it;
			ECalComponentDateTime *edt;
			it = tm_to_icaltimetype (&appt.end, FALSE);
			ical_it = i_cal_object_construct (I_CAL_TYPE_TIME, &it, NULL, TRUE, NULL);
			edt = e_cal_component_datetime_new (ical_it, tzid_str);
			e_cal_component_set_dtend (comp, edt);
			e_cal_component_datetime_free (edt);
			g_object_unref (ical_it);
		}
	}

	/* Recurrence information */
	icalrecurrencetype_clear (&recur);

	switch (appt.repeatType) {
	case repeatNone:
		recur.freq = ICAL_NO_RECURRENCE;
		break;

	case repeatDaily:
		recur.freq = ICAL_DAILY_RECURRENCE;
		recur.interval = appt.repeatFrequency;
		break;

	case repeatWeekly:
		recur.freq = ICAL_WEEKLY_RECURRENCE;
		recur.interval = appt.repeatFrequency;

		pos = 0;
		for (i = 0; i < 7; i++) {
			if (appt.repeatDays[i])
				recur.by_day[pos++] = get_ical_day (i);
		}

		break;

	case repeatMonthlyByDay:
		recur.freq = ICAL_MONTHLY_RECURRENCE;
		recur.interval = appt.repeatFrequency;
		if (appt.repeatDay < domLastSun)
			recur.by_day[0] = nth_weekday ((appt.repeatDay / 7) + 1,
						       get_ical_day (appt.repeatDay % 7));
		else
			recur.by_day[0] = nth_weekday (-1, get_ical_day (appt.repeatDay % 7));
		break;

	case repeatMonthlyByDate:
		recur.freq = ICAL_MONTHLY_RECURRENCE;
		recur.interval = appt.repeatFrequency;
		recur.by_month_day[0] = appt.begin.tm_mday;
		break;

	case repeatYearly:
		recur.freq = ICAL_YEARLY_RECURRENCE;
		recur.interval = appt.repeatFrequency;
		break;

	default:
		g_assert_not_reached ();
	}

	if (recur.freq != ICAL_NO_RECURRENCE) {
		GSList *list = NULL;
		ICalRecurrence *irecur;

		/* recurrence start of week */
		recur.week_start = get_ical_day (appt.repeatWeekstart);

		if (!appt.repeatForever) {
			recur.until = tm_to_icaltimetype (&appt.repeatEnd, TRUE);
		}

		irecur = i_cal_object_construct (I_CAL_TYPE_RECURRENCE, &recur, NULL, TRUE, NULL);
		list = g_slist_append (list, irecur);
		e_cal_component_set_rrules (comp, list);

		/* If the desktop uses count and rrules are
		 * equivalent, use count still on the desktop */
		if (!appt.repeatForever && in_comp && e_cal_component_has_rrules (in_comp)) {
			GSList *existing;
			ICalRecurrence *existing_irecur;
			struct icalrecurrencetype *erecur;

			existing = e_cal_component_get_rrules (in_comp);
			existing_irecur = existing->data;
			erecur = (struct icalrecurrencetype *) i_cal_object_get_native (I_CAL_OBJECT (existing_irecur));

			/* If the rules are otherwise the same and the existing uses count,
			   see if they end at the same point */
			if (rrules_mostly_equal (&recur, erecur) &&
			    icaltime_is_null_time (erecur->until) && erecur->count > 0) {
				time_t last, elast;
				ICalTimezone *itz = i_cal_object_construct (
					I_CAL_TYPE_TIMEZONE, timezone, NULL, TRUE, NULL);

				e_cal_recur_generate_instances_sync (
							e_cal_component_get_icalcomponent (comp),
							NULL, NULL,
							find_last_cb, &last,
							resolve_tzid_cb, client,
							itz, NULL, NULL);
				e_cal_recur_generate_instances_sync (
							e_cal_component_get_icalcomponent (in_comp),
							NULL, NULL,
							find_last_cb, &elast,
							resolve_tzid_cb, client,
							itz, NULL, NULL);
				g_object_unref (itz);

				if (last == elast) {
					ICalRecurrence *count_irecur;
					GSList *count_list = NULL;
					recur.until = icaltime_null_time ();
					recur.count = erecur->count;
					count_irecur = i_cal_object_construct (I_CAL_TYPE_RECURRENCE, &recur, NULL, TRUE, NULL);
					count_list = g_slist_append (count_list, count_irecur);
					e_cal_component_set_rrules (comp, count_list);
					g_slist_free_full (count_list, g_object_unref);
				}
			}
			g_slist_free_full (existing, g_object_unref);
		}

		g_slist_free_full (list, g_object_unref);
	} else {
		e_cal_component_set_rrules (comp, NULL);
	}

	/* Exceptions */
	for (i = 0; i < appt.exceptions; i++) {
		struct tm ex;
		struct icaltimetype ex_itt;
		ICalTime *ical_ex;
		ECalComponentDateTime *ex_dt;

		ex = appt.exception[i];
		ex_itt = tm_to_icaltimetype (&ex, TRUE);
		ical_ex = i_cal_object_construct (I_CAL_TYPE_TIME, &ex_itt, NULL, TRUE, NULL);
		ex_dt = e_cal_component_datetime_new (ical_ex, NULL);

		edl = g_slist_prepend (edl, ex_dt);
		g_object_unref (ical_ex);
	}
	e_cal_component_set_exdates (comp, edl);
	g_slist_free_full (edl, e_cal_component_datetime_free);

	/* Alarm */
	if (appt.alarm) {
		ECalComponentAlarm *alarm = NULL;
		ECalComponentAlarmTrigger *trigger;
		gboolean found = FALSE;

		if (e_cal_component_has_alarms (comp)) {
			GSList *uids, *sl;

			uids = e_cal_component_get_alarm_uids (comp);
			for (sl = uids; sl != NULL; sl = sl->next) {
				alarm = e_cal_component_get_alarm (comp, sl->data);
				trigger = e_cal_component_alarm_get_trigger (alarm);
				if (trigger &&
				    e_cal_component_alarm_trigger_get_kind (trigger) == E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START) {
					ICalDuration *dur = e_cal_component_alarm_trigger_get_duration (trigger);
					if (dur && i_cal_duration_is_neg (dur)) {
						found = TRUE;
						break;
					}
				}
				e_cal_component_alarm_free (alarm);
			}
			g_slist_free_full (uids, g_free);
		}
		if (!found)
			alarm = e_cal_component_alarm_new ();

		{
			ICalDuration *dur = i_cal_duration_new_null_duration ();
			i_cal_duration_set_is_neg (dur, TRUE);
			switch (appt.advanceUnits) {
			case advMinutes:
				i_cal_duration_set_minutes (dur, appt.advance);
				break;
			case advHours:
				i_cal_duration_set_hours (dur, appt.advance);
				break;
			case advDays:
				i_cal_duration_set_days (dur, appt.advance);
				break;
			}
			trigger = e_cal_component_alarm_trigger_new_relative (E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START, dur);
			e_cal_component_alarm_set_trigger (alarm, trigger);
			e_cal_component_alarm_trigger_free (trigger);
			g_object_unref (dur);
		}
		e_cal_component_alarm_set_action (alarm, E_CAL_COMPONENT_ALARM_DISPLAY);

		if (!found)
			e_cal_component_add_alarm (comp, alarm);
		e_cal_component_alarm_free (alarm);
	}

	e_cal_component_set_transparency (comp, E_CAL_COMPONENT_TRANSP_NONE);

	if (remote->secret)
		e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_PRIVATE);
	else
		e_cal_component_set_classification (comp, E_CAL_COMPONENT_CLASS_PUBLIC);

	e_cal_component_commit_sequence (comp);

	free_Appointment (&appt);

	return comp;
}

static void
check_for_slow_setting (GnomePilotConduit *c, ECalConduitContext *ctxt)
{
	GnomePilotConduitStandard *conduit = GNOME_PILOT_CONDUIT_STANDARD (c);
	gint map_count;
	const gchar *uri;

	/* If there are objects but no log */
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
	  ECalConduitContext *ctxt)
{
	GnomePilotConduitSyncAbs *abs_conduit;
	GList *removed = NULL, *added = NULL, *l;
	gint len;
	guchar *buf;
	gchar *filename, *change_id;
	ICalComponent *icalcomp;
	gint num_records, add_records = 0, mod_records = 0, del_records = 0;
	pi_buffer_t * buffer;
	abs_conduit = GNOME_PILOT_CONDUIT_SYNC_ABS (conduit);

	LOG (g_message ( "---------------------------------------------------------\n" ));
	LOG (g_message ( "pre_sync: Calendar Conduit v.%s", CONDUIT_VERSION ));

	ctxt->dbi = dbi;
	if (NULL == dbi->pilotInfo->pilot_charset)
		ctxt->pilot_charset = NULL;
	else
		 ctxt->pilot_charset = g_strdup(dbi->pilotInfo->pilot_charset);
	ctxt->client = NULL;

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

	ctxt->default_comp = e_cal_component_new ();
	if (!e_cal_component_set_icalcomponent (ctxt->default_comp, icalcomp)) {
		g_object_unref (ctxt->default_comp);
		g_object_unref (icalcomp);
		return -1;
	}

	/* Load the uid <--> pilot id mapping */
	filename = map_name (ctxt);
	e_pilot_map_read (filename, &ctxt->map);
	g_free (filename);

	/* Get the local database */
	if (!e_cal_client_get_object_list_as_comps_sync (ctxt->client, "#t", &ctxt->comps, NULL, NULL))
		return -1;

	/* Build change list from all components.
	 * NOTE: e_cal_get_changes() was removed in modern EDS.
	 * We treat all existing objects as modified, which effectively
	 * forces a slow sync approach for change detection.
	 */
	change_id = g_strdup_printf ("pilot-sync-evolution-calendar-%d", ctxt->cfg->pilot_id);
	ctxt->changed = NULL;
	for (l = ctxt->comps; l != NULL; l = l->next) {
		ECalChange *ccc = g_new0 (ECalChange, 1);
		ccc->comp = g_object_ref (l->data);
		ccc->type = E_CAL_CHANGE_MODIFIED;
		ctxt->changed = g_list_prepend (ctxt->changed, ccc);
	}
	ctxt->changed_hash = g_hash_table_new (g_str_hash, g_str_equal);
	g_free (change_id);

	/* See if we need to split up any events */
	for (l = ctxt->changed; l != NULL; l = l->next) {
		ECalChange *ccc = l->data;
		GList *multi_comp = NULL, *multi_ccc = NULL;

		if (process_multi_day (ctxt, ccc, &multi_comp, &multi_ccc)) {
			ctxt->comps = g_list_concat (ctxt->comps, multi_comp);

			added = g_list_concat (added, multi_ccc);
			removed = g_list_prepend (removed, ccc);
		}
	}

	/* Remove the events that were split up */
	ctxt->changed = g_list_concat (ctxt->changed, added);
	for (l = removed; l != NULL; l = l->next) {
		ECalChange *ccc = l->data;
		const gchar *uid;

		uid = e_cal_component_get_uid (ccc->comp);
		if (e_pilot_map_lookup_pid (ctxt->map, uid, FALSE) == 0) {
			ctxt->changed = g_list_remove (ctxt->changed, ccc);
			g_object_unref (ccc->comp);
			g_free (ccc);
		}
	}
	g_list_free (removed);

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

	buffer = pi_buffer_new(DLP_BUF_SIZE);
	if (buffer == NULL) {
		pi_set_error(dbi->pilot_socket, PI_ERR_GENERIC_MEMORY);
		return -1;
	}

	len = dlp_ReadAppBlock (dbi->pilot_socket, dbi->db_handle, 0,
				DLP_BUF_SIZE,
				buffer);
	if (len < 0) {
		WARN (_("Could not read pilot's Calendar application block"));
		WARN ("dlp_ReadAppBlock(...) = %d", len);
		gnome_pilot_conduit_error (conduit,
					   _("Could not read pilot's Calendar application block"));
		return -1;
	}
	buf = g_new0 (unsigned char,buffer->used);
	memcpy(buf, buffer->data, buffer->used);
	pi_buffer_free(buffer);
	unpack_AppointmentAppInfo (&(ctxt->ai), buf, len);
	/* unpack_CategoryAppInfo (&(ctxt->ai.category), buf, len); */
	g_free (buf);

	check_for_slow_setting (conduit, ctxt);
	if (ctxt->cfg->sync_type == GnomePilotConduitSyncTypeCopyToPilot
	    || ctxt->cfg->sync_type == GnomePilotConduitSyncTypeCopyFromPilot)
		ctxt->map->write_touched_only = TRUE;

	return 0;
}

static gint
post_sync (GnomePilotConduit *conduit,
	   GnomePilotDBInfo *dbi,
	   ECalConduitContext *ctxt)
{
	gchar *filename;
	guchar *buf;
	gint dlpRetVal, len;

	LOG (g_message ( "post_sync: Calendar Conduit v.%s", CONDUIT_VERSION ));

	/* Write AppBlock to PDA - updates categories */
	buf = (guchar *)g_malloc (0xffff);

	len = pack_AppointmentAppInfo (&(ctxt->ai), buf, 0xffff);

	dlpRetVal = dlp_WriteAppBlock (dbi->pilot_socket, dbi->db_handle,
			      (guchar *)buf, len);

	g_free (buf);

	if (dlpRetVal < 0) {
		WARN ( ("Could not write pilot's Calendar application block"));
		WARN ("dlp_WriteAppBlock(...) = %d", dlpRetVal);
		/*gnome_pilot_conduit_error (conduit,
					   _("Could not write pilot's Calendar application block"));*/
		return -1;
	}

	g_free (ctxt->cfg->last_uri);
	{
		ESource *source = e_client_get_source (E_CLIENT (ctxt->client));
		ctxt->cfg->last_uri = g_strdup (e_source_get_uid (source));
	}
	calconduit_save_configuration (ctxt->cfg);

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
	      ECalLocalRecord *local,
	      guint32 ID,
	      ECalConduitContext *ctxt)
{
	const gchar *uid;

	LOG (g_message ( "set_pilot_id: setting to %d\n", ID ));

	uid = e_cal_component_get_uid (local->comp);
	e_pilot_map_insert (ctxt->map, ID, uid, FALSE);

        return 0;
}

static gint
set_status_cleared (GnomePilotConduitSyncAbs *conduit,
		    ECalLocalRecord *local,
		    ECalConduitContext *ctxt)
{
	const gchar *uid;

	LOG (g_message ( "set_status_cleared: clearing status\n" ));

	uid = e_cal_component_get_uid (local->comp);
	g_hash_table_remove (ctxt->changed_hash, uid);

        return 0;
}

static gint
for_each (GnomePilotConduitSyncAbs *conduit,
	  ECalLocalRecord **local,
	  ECalConduitContext *ctxt)
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

			*local = g_new0 (ECalLocalRecord, 1);
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

			*local = g_new0 (ECalLocalRecord, 1);
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
		   ECalLocalRecord **local,
		   ECalConduitContext *ctxt)
{
	static GList *iterator;
	static gint count;
        GList *unused;

	g_return_val_if_fail (local != NULL, -1);

	if (*local == NULL) {
		LOG (g_message ( "for_each_modified beginning\n" ));

		iterator = ctxt->changed;

		count = 0;

		LOG (g_message ( "iterating over %d records", g_hash_table_size (ctxt->changed_hash) ));

		iterator = next_changed_item (ctxt, iterator);
		if (iterator != NULL) {
			ECalChange *ccc = iterator->data;

			*local = g_new0 (ECalLocalRecord, 1);
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

			*local = g_new0 (ECalLocalRecord, 1);
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
	 ECalLocalRecord *local,
	 GnomePilotRecord *remote,
	 ECalConduitContext *ctxt)
{
	/* used by the quick compare */
	GnomePilotRecord local_pilot;
	gint retval = 0;

	LOG (g_message ("compare: local=%s remote=%s...\n",
			print_local (local), print_remote (remote)));

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
	    ECalConduitContext *ctxt)
{
	ECalComponent *comp;
	gchar *uid;
	gint retval = 0;

	g_return_val_if_fail (remote != NULL, -1);

	LOG (g_message ( "add_record: adding %s to desktop\n", print_remote (remote) ));

	comp = comp_from_remote_record (conduit, remote, ctxt->default_comp, ctxt->client, ctxt->timezone, &(ctxt->ai.category), ctxt->pilot_charset);

	/* Give it a new UID otherwise it will be the uid of the default comp */
	uid = e_util_generate_uid ();
	e_cal_component_set_uid (comp, uid);

	if (!e_cal_client_create_object_sync (ctxt->client, e_cal_component_get_icalcomponent (comp), E_CAL_OPERATION_FLAG_NONE, NULL, NULL, NULL))
		return -1;

	e_pilot_map_insert (ctxt->map, remote->ID, uid, FALSE);

	g_free (uid);

	g_object_unref (comp);

	return retval;
}

static gint
replace_record (GnomePilotConduitSyncAbs *conduit,
		ECalLocalRecord *local,
		GnomePilotRecord *remote,
		ECalConduitContext *ctxt)
{
	ECalComponent *new_comp;
	gint retval = 0;

	g_return_val_if_fail (remote != NULL, -1);

	LOG (g_message ("replace_record: replace %s with %s\n",
			print_local (local), print_remote (remote)));

	new_comp = comp_from_remote_record (conduit, remote, local->comp, ctxt->client, ctxt->timezone, &(ctxt->ai.category),
ctxt->pilot_charset);
	g_object_unref (local->comp);
	local->comp = new_comp;

	if (!e_cal_client_modify_object_sync (ctxt->client, e_cal_component_get_icalcomponent (new_comp),
				       E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE, NULL, NULL))
		return -1;

	return retval;
}

static gint
delete_record (GnomePilotConduitSyncAbs *conduit,
	       ECalLocalRecord *local,
	       ECalConduitContext *ctxt)
{
	const gchar *uid;

	g_return_val_if_fail (local != NULL, -1);
	g_assert (local->comp != NULL);

	uid = e_cal_component_get_uid (local->comp);

	LOG (g_message ( "delete_record: deleting %s\n", uid ));

	e_pilot_map_remove_by_uid (ctxt->map, uid);
	/* FIXME Error handling */
	e_cal_client_remove_object_sync (ctxt->client, uid, NULL, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE, NULL, NULL);

        return 0;
}

static gint
archive_record (GnomePilotConduitSyncAbs *conduit,
		ECalLocalRecord *local,
		gboolean archive,
		ECalConduitContext *ctxt)
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
       ECalLocalRecord **local,
       ECalConduitContext *ctxt)
{
	const gchar *uid;

	LOG (g_message ("match: looking for local copy of %s\n",
			print_remote (remote)));

	g_return_val_if_fail (local != NULL, -1);
	g_return_val_if_fail (remote != NULL, -1);

	*local = NULL;
	uid = e_pilot_map_lookup_uid (ctxt->map, remote->ID, TRUE);

	if (!uid)
		return 0;

	LOG (g_message ( "  matched\n" ));

	*local = g_new0 (ECalLocalRecord, 1);
	local_record_from_uid (*local, uid, ctxt);

	return 0;
}

static gint
free_match (GnomePilotConduitSyncAbs *conduit,
	    ECalLocalRecord *local,
	    ECalConduitContext *ctxt)
{
	LOG (g_message ( "free_match: freeing\n" ));

	g_return_val_if_fail (local != NULL, -1);

	ctxt->locals = g_list_remove (ctxt->locals, local);

	calconduit_destroy_record (local);

	return 0;
}

static gint
prepare (GnomePilotConduitSyncAbs *conduit,
	 ECalLocalRecord *local,
	 GnomePilotRecord *remote,
	 ECalConduitContext *ctxt)
{
	LOG (g_message ( "prepare: encoding local %s\n", print_local (local) ));

	*remote = local_record_to_pilot_record (local, ctxt);

	return 0;
}

/* Pilot Settings Callbacks */
static void
fill_widgets (ECalConduitContext *ctxt)
{
	if (ctxt->cfg->source)
		e_pilot_settings_set_source (E_PILOT_SETTINGS (ctxt->ps),
					     ctxt->cfg->source);
	e_pilot_settings_set_secret (E_PILOT_SETTINGS (ctxt->ps),
				     ctxt->cfg->secret);

	e_cal_gui_fill_widgets (ctxt->gui, ctxt->cfg);
}

static gint
create_settings_window (GnomePilotConduit *conduit,
			GtkWidget *parent,
			ECalConduitContext *ctxt)
{
	LOG (g_message ( "create_settings_window" ));

	if (!ctxt->cfg->registry)
		return -1;

	ctxt->ps = e_pilot_settings_new (ctxt->cfg->registry, E_SOURCE_EXTENSION_CALENDAR);
	ctxt->gui = e_cal_gui_new (E_PILOT_SETTINGS (ctxt->ps));

	if (GTK_IS_BOX (parent))
		gtk_box_append (GTK_BOX (parent), ctxt->ps);
	else
		gtk_widget_set_parent (ctxt->ps, parent);

	fill_widgets (ctxt);

	return 0;
}
static void
display_settings (GnomePilotConduit *conduit, ECalConduitContext *ctxt)
{
	LOG (g_message ( "display_settings" ));

	fill_widgets (ctxt);
}

static void
save_settings    (GnomePilotConduit *conduit, ECalConduitContext *ctxt)
{
        LOG (g_message ( "save_settings" ));

	if (ctxt->new_cfg->source)
		g_object_unref (ctxt->new_cfg->source);
	ctxt->new_cfg->source = g_object_ref (e_pilot_settings_get_source (E_PILOT_SETTINGS (ctxt->ps)));
	g_object_ref (ctxt->new_cfg->source);
	ctxt->new_cfg->secret =
		e_pilot_settings_get_secret (E_PILOT_SETTINGS (ctxt->ps));
	e_cal_gui_fill_config (ctxt->gui, ctxt->new_cfg);

	calconduit_save_configuration (ctxt->new_cfg);
}

static void
revert_settings  (GnomePilotConduit *conduit, ECalConduitContext *ctxt)
{
	LOG (g_message ( "revert_settings" ));

	calconduit_save_configuration (ctxt->cfg);
	calconduit_destroy_configuration (ctxt->new_cfg);
	ctxt->new_cfg = calconduit_dupe_configuration (ctxt->cfg);
}

GnomePilotConduit *
conduit_get_gpilot_conduit (guint32 pilot_id)
{
	GObject *retval;
	ECalConduitContext *ctxt;

	LOG (g_message ( "in calendar's conduit_get_gpilot_conduit\n" ));

	retval = gnome_pilot_conduit_sync_abs_new ((gchar *)"DatebookDB", 0x64617465);
	g_assert (retval != NULL);

	ctxt = e_calendar_context_new (pilot_id);
	g_object_set_data (G_OBJECT (retval), "calconduit_context", ctxt);

	/* Sync signals */
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
	ECalConduitContext *ctxt;

	ctxt = g_object_get_data (obj, "calconduit_context");
	e_calendar_context_destroy (ctxt);

	g_object_unref (obj);
}
