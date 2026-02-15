/* gnome-pilot-conduit-standard-abs.c
 * Copyright (C) 1999  Red Hat, Inc.
 * Copyright (C) 2000  Free Software Foundation
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
 *          Johnathan Blandford
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>

#include <pi-source.h>
#include <pi-socket.h>
#include <pi-dlp.h>
#include "gnome-pilot-conduit-standard-abs.h"
#include "gpmarshal.h"
#include "manager.h"


/* Compatibility routines for old API in pilot-link-0.11 */
extern int
gnome_pilot_compat_with_pilot_link_0_11_dlp_ReadRecordById (int sd, int fHandle, recordid_t id, void *buffer,
							    int *index, int *size, int *attr, int *category);

extern int
gnome_pilot_compat_with_pilot_link_0_11_dlp_ReadRecordByIndex (int sd, int fHandle, int index, void *buffer,
							       recordid_t * id, int *size, int *attr,
							       int *category);
extern int
gnome_pilot_compat_with_pilot_link_0_11_dlp_ReadNextModifiedRec (int sd, int fHandle, void *buffer, recordid_t * id,
								 int *index, int *size, int *attr, int *category);


int
gnome_pilot_compat_with_pilot_link_0_11_dlp_ReadRecordById (int sd, int fHandle, recordid_t id, void *buffer,
							    int *index, int *size, int *attr, int *category)
{
	pi_buffer_t *pi_buf = pi_buffer_new (0xffff);
	int result = dlp_ReadRecordById (sd, fHandle, id, 
					 pi_buf,
					 index, attr, category);
	memcpy (buffer, pi_buf->data, pi_buf->used);
	if (NULL!=size) {
		*size = pi_buf->used;
	}
	pi_buffer_free (pi_buf);

	return result;
}

int
gnome_pilot_compat_with_pilot_link_0_11_dlp_ReadRecordByIndex (int sd, int fHandle, int index, void *buffer,
							       recordid_t * id, int *size, int *attr,
							       int *category)
{
	pi_buffer_t *pi_buf = pi_buffer_new (0xffff);
	int result = dlp_ReadRecordByIndex (sd, fHandle, index, 
					    pi_buf,
					    id, attr, category);
	memcpy (buffer, pi_buf->data, pi_buf->used);
	if (NULL!=size) {
		*size = pi_buf->used;
	}
	pi_buffer_free (pi_buf);

	return result;
}

int
gnome_pilot_compat_with_pilot_link_0_11_dlp_ReadNextModifiedRec (int sd, int fHandle, void *buffer, recordid_t * id,
								 int *index, int *size, int *attr, int *category)
{
	pi_buffer_t *pi_buf = pi_buffer_new (0xffff);
	int result = dlp_ReadNextModifiedRec (sd, fHandle, 
					      pi_buf, 
					      id, index, attr, category);
	memcpy (buffer, pi_buf->data, pi_buf->used);
	if (NULL!=size) {
		*size = pi_buf->used;
	}
	pi_buffer_free (pi_buf);

	return result;
}

enum {
	MATCH_RECORD,
	FREE_MATCH,
	ARCHIVE_LOCAL,
	ARCHIVE_REMOTE,
	STORE_REMOTE,
	ITERATE,
	ITERATE_SPECIFIC,
	PURGE,
	SET_STATUS,
	SET_PILOT_ID,
	COMPARE,
	COMPARE_BACKUP,
	FREE_TRANSMIT,
	DELETE_ALL,
	TRANSMIT,
	PRE_SYNC,
	LAST_SIGNAL
};

enum StandardAbsSyncDirection {
	SyncToRemote = 0x01,
	SyncToLocal  = 0x02
};

typedef enum StandardAbsSyncDirection StandardAbsSyncDirection;
#define SyncBothWays SyncToRemote|SyncToLocal


#define LOG_CASE5 _("deleted PDA record modified locally, not deleting\n")
#define LOG_CASE6 _("deleted local record modified on PDA, not deleting\n")
#define LOG_CASE10 _("merge conflict, PDA and local record swapped\n")
#define LOG_CASE13 _("archive and change conflict, PDA and local record swapped\n")
#define LOG_CASE15 _("archive and change conflict, local record sent to PDA\n")
#define LOG_CASE18 _("archive and change conflict, PDA record saved locally\n")
#define LOG_CASE20 _("merge conflict, PDA and local record swapped\n")

#define Result(v) result = v; break

#define Finish                                                             \
if (result == 0) {                                                         \
	standard_abs_close_db_and_purge_local (conduit, dbinfo, TRUE);     \
} else {                                                                   \
	standard_abs_close_db_and_purge_local (conduit, dbinfo, FALSE);    \
}                                                                          \
return result

static void gnome_pilot_conduit_standard_abs_init		(GnomePilotConduitStandardAbs		 *pilot_conduit_standard_abs);
static void gnome_pilot_conduit_standard_abs_class_init		(GnomePilotConduitStandardAbsClass	 *klass);
static gint gnome_pilot_conduit_standard_real_copy_to_pilot     (GnomePilotConduitStandard *conduit,
								 GnomePilotDBInfo  *dbinfo);
static gint gnome_pilot_conduit_standard_real_copy_from_pilot   (GnomePilotConduitStandard *conduit,
								 GnomePilotDBInfo  *dbinfo);
static gint gnome_pilot_conduit_standard_real_merge_to_pilot    (GnomePilotConduitStandard *conduit,
								 GnomePilotDBInfo  *dbinfo);
static gint gnome_pilot_conduit_standard_real_merge_from_pilot  (GnomePilotConduitStandard *conduit,
								 GnomePilotDBInfo  *dbinfo);
static gint gnome_pilot_conduit_standard_real_synchronize       (GnomePilotConduitStandard *conduit,
								 GnomePilotDBInfo  *dbinfo);

int standard_abs_sync_record (GnomePilotConduitStandardAbs *, int, int, LocalRecord *, PilotRecord *, StandardAbsSyncDirection);
gint standard_abs_check_locally_deleted_records(GnomePilotConduitStandardAbs *, int, int, StandardAbsSyncDirection);
gint standard_abs_merge_to_remote (GnomePilotConduitStandardAbs *, int, int, StandardAbsSyncDirection);
gint standard_abs_merge_to_local (GnomePilotConduitStandardAbs *, int, int, StandardAbsSyncDirection);
int SlowSync (int, int, GnomePilotConduitStandardAbs *);
gint FastSync (int, int, GnomePilotConduitStandardAbs *);
gboolean gpilot_sync_pc_match(GnomePilotDBInfo*);
gint standard_abs_open_db(GnomePilotConduitStandardAbs*,GnomePilotDBInfo*);
void standard_abs_close_db_and_purge_local(GnomePilotConduitStandardAbs*,GnomePilotDBInfo*, gboolean);

static GnomePilotConduitStandardClass *parent_class = NULL;
static guint pilot_conduit_standard_abs_signals[LAST_SIGNAL] = { 0 };

GType
gnome_pilot_conduit_standard_abs_get_type (void)
{
	static GType pilot_conduit_standard_abs_type = 0;

	if (!pilot_conduit_standard_abs_type)
	{
		static const GTypeInfo pilot_conduit_standard_abs_info =
		{
			sizeof (GnomePilotConduitStandardAbsClass),
			NULL,
			NULL,
			(GClassInitFunc) gnome_pilot_conduit_standard_abs_class_init,
			NULL,
			NULL,
			sizeof (GnomePilotConduitStandardAbs),
			0,
			(GInstanceInitFunc) gnome_pilot_conduit_standard_abs_init,
		};

		pilot_conduit_standard_abs_type = g_type_register_static (
		    gnome_pilot_conduit_standard_get_type (),
		    "GnomePilotConduitStandardAbs", &pilot_conduit_standard_abs_info, 0);
	}

	return pilot_conduit_standard_abs_type;
}

GObject *
gnome_pilot_conduit_standard_abs_new (char *db_name,
				      guint32 creator_id)
{
	GObject *retval;

	retval =  g_object_new (gnome_pilot_conduit_standard_abs_get_type (),
				  "GnomePilotConduitStandard::db_name", db_name,
				  "GnomePilotConduitStandard::creator_id", creator_id,
				  NULL);

	return retval;
}

static void
gnome_pilot_conduit_standard_abs_class_init (GnomePilotConduitStandardAbsClass *klass)
{
	GObjectClass *object_class;
	GnomePilotConduitStandardClass *conduit_standard_class;

	object_class = (GObjectClass*) klass;
	conduit_standard_class = (GnomePilotConduitStandardClass *) klass;

	parent_class = g_type_class_peek (gnome_pilot_conduit_standard_get_type ());

	pilot_conduit_standard_abs_signals[MATCH_RECORD] =
		g_signal_new   ("match_record",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitStandardAbsClass, match_record),
				NULL,
				NULL,
				gp_marshal_INT__POINTER_POINTER,
				G_TYPE_INT, 2, G_TYPE_POINTER, G_TYPE_POINTER);

	pilot_conduit_standard_abs_signals[FREE_MATCH] =
		g_signal_new   ("free_match",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitStandardAbsClass, free_match),
				NULL,
				NULL,
				gp_marshal_INT__POINTER,
				G_TYPE_INT, 1, G_TYPE_POINTER);

	pilot_conduit_standard_abs_signals[ARCHIVE_LOCAL] =
		g_signal_new   ("archive_local",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitStandardAbsClass, archive_local),
				NULL,
				NULL,
				gp_marshal_INT__POINTER,
				G_TYPE_INT, 1, G_TYPE_POINTER);

	pilot_conduit_standard_abs_signals[ARCHIVE_REMOTE] =
		g_signal_new   ("archive_remote",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitStandardAbsClass, archive_remote),
				NULL,
				NULL,
				gp_marshal_INT__POINTER_POINTER,
				G_TYPE_INT, 2, G_TYPE_POINTER, G_TYPE_POINTER);

	pilot_conduit_standard_abs_signals[STORE_REMOTE] =
		g_signal_new   ("store_remote",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitStandardAbsClass, store_remote),
				NULL,
				NULL,
				gp_marshal_INT__POINTER,
				G_TYPE_INT, 1, G_TYPE_POINTER);

	pilot_conduit_standard_abs_signals[ITERATE] =
		g_signal_new   ("iterate",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitStandardAbsClass, iterate),
				NULL,
				NULL,
				gp_marshal_INT__POINTER,
				G_TYPE_INT, 1, G_TYPE_POINTER);

	pilot_conduit_standard_abs_signals[ITERATE_SPECIFIC] =
		g_signal_new   ("iterate_specific",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitStandardAbsClass, iterate_specific),
				NULL,
				NULL,
				gp_marshal_INT__POINTER_INT_INT,
				G_TYPE_INT, 3, G_TYPE_POINTER, G_TYPE_INT, G_TYPE_INT);

	pilot_conduit_standard_abs_signals[PURGE] =
		g_signal_new   ("purge",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitStandardAbsClass, purge),
				NULL,
				NULL,
				gp_marshal_INT__NONE,
				G_TYPE_INT, 0);

	pilot_conduit_standard_abs_signals[SET_STATUS] =
		g_signal_new   ("set_status",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitStandardAbsClass, set_status),
				NULL,
				NULL,
				gp_marshal_INT__POINTER_INT,
				G_TYPE_INT, 2, G_TYPE_POINTER, G_TYPE_INT);

	pilot_conduit_standard_abs_signals[SET_PILOT_ID] =
		g_signal_new   ("set_pilot_id",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitStandardAbsClass, set_pilot_id),
				NULL,
				NULL,
				gp_marshal_INT__POINTER_INT,
				G_TYPE_INT, 2, G_TYPE_POINTER, G_TYPE_INT);

	pilot_conduit_standard_abs_signals[COMPARE] =
		g_signal_new   ("compare",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitStandardAbsClass, compare),
				NULL,
				NULL,
				gp_marshal_INT__POINTER_POINTER,
				G_TYPE_INT, 2, G_TYPE_POINTER, G_TYPE_POINTER);

	pilot_conduit_standard_abs_signals[COMPARE_BACKUP] =
		g_signal_new   ("compare_backup",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitStandardAbsClass, compare_backup),
				NULL,
				NULL,
				gp_marshal_INT__POINTER_POINTER,
				G_TYPE_INT, 2, G_TYPE_POINTER, G_TYPE_POINTER);

	pilot_conduit_standard_abs_signals[FREE_TRANSMIT] =
		g_signal_new   ("free_transmit",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitStandardAbsClass, free_transmit),
				NULL,
				NULL,
				gp_marshal_INT__POINTER_POINTER,
				G_TYPE_INT, 2, G_TYPE_POINTER, G_TYPE_POINTER);

	pilot_conduit_standard_abs_signals[DELETE_ALL] =
		g_signal_new   ("delete_all",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitStandardAbsClass, delete_all),
				NULL,
				NULL,
				gp_marshal_INT__NONE,
				G_TYPE_INT, 0);

	pilot_conduit_standard_abs_signals[TRANSMIT] =
		g_signal_new   ("transmit",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitStandardAbsClass, transmit),
				NULL,
				NULL,
				gp_marshal_INT__POINTER_POINTER,
				G_TYPE_INT, 2, G_TYPE_POINTER, G_TYPE_POINTER);

	pilot_conduit_standard_abs_signals[PRE_SYNC] =
		g_signal_new   ("pre_sync",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitStandardAbsClass, pre_sync),
				NULL,
				NULL,
				gp_marshal_INT__POINTER,
				G_TYPE_INT, 1, G_TYPE_POINTER);

	conduit_standard_class->copy_to_pilot = gnome_pilot_conduit_standard_real_copy_to_pilot;
	conduit_standard_class->copy_from_pilot = gnome_pilot_conduit_standard_real_copy_from_pilot;
	conduit_standard_class->merge_to_pilot = gnome_pilot_conduit_standard_real_merge_to_pilot;
	conduit_standard_class->merge_from_pilot = gnome_pilot_conduit_standard_real_merge_from_pilot;
	conduit_standard_class->synchronize = gnome_pilot_conduit_standard_real_synchronize;
}


/*
  Given PilotRecord with a "fresh" attr field, set by a dlp call,
  sets secret, archived and attr to GnomePilot values
 */
static void
standard_abs_compute_attr_field(PilotRecord *remote)
{
	remote->secret = remote->attr & dlpRecAttrSecret;
	remote->archived = remote->attr & dlpRecAttrArchived;
	if (remote->attr & dlpRecAttrDeleted)
		remote->attr = GnomePilotRecordDeleted;
	else if (remote->attr & dlpRecAttrDirty)
		remote->attr = GnomePilotRecordModified;
	else
		remote->attr = GnomePilotRecordNothing;
}

static void
gnome_pilot_conduit_standard_abs_init (GnomePilotConduitStandardAbs *conduit)
{
	conduit->record_ids_to_ignore = NULL;
	conduit->total_records = 0;
	conduit->num_local_records = -1;
	conduit->num_updated_local_records = -1;
	conduit->num_new_local_records = -1;
	conduit->num_deleted_local_records = -1;
	conduit->db_open_mode = 0;
	conduit->progress = 0; /* as usual... */
	conduit->total_progress = 0; /* oh no!... */

}

static gint
gnome_pilot_conduit_standard_real_copy_to_pilot     (GnomePilotConduitStandard *conduit_standard,
						     GnomePilotDBInfo  *dbinfo)
{
	GnomePilotConduitStandardAbs *conduit = NULL;
	LocalRecord *local = NULL;
	int result = 0;
	int err = 0;
	recordid_t assigned_id;

	g_return_val_if_fail (conduit_standard != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD_ABS (conduit_standard), -1);

	conduit = GNOME_PILOT_CONDUIT_STANDARD_ABS (conduit_standard);

	do {
		
		/* Open db and prepare conduit */	
		err = standard_abs_open_db(conduit, dbinfo);
		
		if (err < 0) {
			Result(-1);
		}
		
		if (gnome_pilot_conduit_standard_abs_pre_sync(conduit, dbinfo) != 0) {
			g_warning(_("Conduits initialization failed, aborting operation"));
			Result(-2);
		}
		
		/* Set the counters for the progress bar, note, total_records is set
		   in standard_abs_open_db */
		if (conduit->num_local_records == -1) {
			conduit->num_local_records = conduit->total_records;
		}
		conduit->total_progress += conduit->num_updated_local_records;
		
		/* nuke all records */
		if (dlp_DeleteRecord (dbinfo->pilot_socket, dbinfo->db_handle, 1, 0) < 0) {
			g_warning(_("Unable to delete all records in PDA database, aborting operation."));
			Result(-4);
		}
		
		while (gnome_pilot_conduit_standard_abs_iterate (conduit, &local) && local) {
			if (local->archived) {
				err = gnome_pilot_conduit_standard_abs_archive_local (conduit, local);
			} else if (local->attr != GnomePilotRecordDeleted) {
				PilotRecord *remote;
				gnome_pilot_conduit_standard_abs_transmit (conduit, local, &remote);
				if (remote==NULL) {
					g_warning(_("Conduit did not return a record"));
					break;
				}
				gnome_pilot_conduit_standard_abs_set_status (conduit, local, 
									     GnomePilotRecordNothing);
				err = dlp_WriteRecord(dbinfo->pilot_socket,
							 dbinfo->db_handle,
							 remote->secret?dlpRecAttrSecret:0,
							 remote->ID,
							 remote->category,
							 remote->record,
							 remote->length,
							 &assigned_id);
				if (err > 0) {
					gnome_pilot_conduit_standard_abs_set_pilot_id(conduit,local,assigned_id);
				}
				gnome_pilot_conduit_standard_abs_free_transmit (conduit, local, &remote);
			}
		}
		
	} while (0);

	Finish;
}

static gint
gnome_pilot_conduit_standard_real_copy_from_pilot   (GnomePilotConduitStandard *conduit_standard,
						     GnomePilotDBInfo  *dbinfo)
{
	GnomePilotConduitStandardAbs *conduit = NULL;
	unsigned char buffer[0xffff];
	int index = 0;
	int result = 0;
	int err;
	PilotRecord remote;

	g_return_val_if_fail (conduit_standard != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD_ABS (conduit_standard), -1);

	conduit = GNOME_PILOT_CONDUIT_STANDARD_ABS (conduit_standard);

	remote.record = buffer;

	do {
		/* Open db and prepare conduit */
		err = standard_abs_open_db(conduit,dbinfo);
		if (err < 0) {
			Result(-1);
		}

		if (gnome_pilot_conduit_standard_abs_pre_sync(conduit, dbinfo)!=0) {
			g_warning(_("Conduits initialization failed, aborting operation"));
			Result(-2);
		}
		
		/* nuke all records */
		if (gnome_pilot_conduit_standard_abs_delete_all (conduit) < 0) {
			g_warning(_("Unable to delete all records in local database, aborting operation."));
			Result(-3);
		}
		
		/* copy all records */
		while (gnome_pilot_compat_with_pilot_link_0_11_dlp_ReadRecordByIndex (dbinfo->pilot_socket,
					      dbinfo->db_handle,
					      index,
					      remote.record,
					      &remote.ID,
					      &remote.length,
					      &remote.attr,
					      &remote.category) >= 0) {
			standard_abs_compute_attr_field(&remote);
			if (remote.archived) {
				remote.attr = GnomePilotRecordNothing;
				remote.archived = 0;
				gnome_pilot_conduit_standard_abs_archive_remote (conduit, 
										 NULL, 
										 &remote);
			} else if (remote.attr != GnomePilotRecordDeleted) {
				remote.attr = GnomePilotRecordNothing;
				remote.archived = 0;
				gnome_pilot_conduit_standard_abs_store_remote (conduit, &remote);
			}
			index++;
			gnome_pilot_conduit_send_progress(GNOME_PILOT_CONDUIT(conduit),
							  conduit->total_records,
							  ++conduit->progress);
		}		
	} while (0);

	Finish;
}

static gint
gnome_pilot_conduit_standard_real_merge_to_pilot    (GnomePilotConduitStandard *conduit_standard,
						     GnomePilotDBInfo  *dbinfo)
{
	GnomePilotConduitStandardAbs *conduit = NULL;
	int result = 0;
	int err = 0;

	g_return_val_if_fail (conduit_standard != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD_ABS (conduit_standard), -1);	

	conduit = GNOME_PILOT_CONDUIT_STANDARD_ABS (conduit_standard);

	do {
		/* Open db and prepare conduit */
		err = standard_abs_open_db(conduit, dbinfo);
		if (err < 0) {
			Result(-1);
		}
		if (gnome_pilot_conduit_standard_abs_pre_sync(conduit, dbinfo)!=0) {
			g_warning(_("Conduits initialization failed, aborting operation"));
			Result(-2);
		}
		standard_abs_merge_to_remote(conduit, dbinfo->pilot_socket, dbinfo->db_handle, SyncToRemote );
	} while (0);
	
	Finish;
}

static gint
gnome_pilot_conduit_standard_real_merge_from_pilot  (GnomePilotConduitStandard *conduit_standard,
						     GnomePilotDBInfo  *dbinfo)
{
	GnomePilotConduitStandardAbs *conduit = NULL;
	int result = 0;
	int err = 0;

	g_return_val_if_fail (conduit_standard != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD_ABS (conduit_standard), -1);

	conduit = GNOME_PILOT_CONDUIT_STANDARD_ABS (conduit_standard);

	do {
		/* Open db and prepare conduit */
		err = standard_abs_open_db(conduit, dbinfo);
		if (err < 0) {
			Result(-1);
		}
		if (gnome_pilot_conduit_standard_abs_pre_sync(conduit, dbinfo)!=0) {
			g_warning(_("Conduits initialization failed, aborting operation"));
			Result(-2);
		}
		standard_abs_merge_to_local(conduit, dbinfo->pilot_socket, dbinfo->db_handle, SyncToLocal );
	} while (0);		

	Finish;
}

static gint
gnome_pilot_conduit_standard_real_synchronize       (GnomePilotConduitStandard *conduit_standard,
						     GnomePilotDBInfo  *dbinfo)
{
	int result = 0;
	int err = 0;
	GnomePilotConduitStandardAbs *conduit = NULL;

	g_return_val_if_fail (conduit_standard != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD_ABS (conduit_standard), -1);

	conduit = GNOME_PILOT_CONDUIT_STANDARD_ABS(conduit_standard);

	do {
		/* Open db and prepare conduit */
		err = standard_abs_open_db(conduit, dbinfo);
		if (err < 0) {
			Result(-1);
		}
		if (gnome_pilot_conduit_standard_abs_pre_sync(conduit, dbinfo)!=0) {
			g_warning(_("Conduits initialization failed, aborting operation"));
			Result(-2);
		}
		
		/* Set the counters for the progress bar, note, total_records is set
		   in standard_abs_open_db */
		conduit->total_progress = 0;
		if (conduit->num_local_records == -1) {
			conduit->num_local_records = conduit->total_records;
		}
		conduit->total_progress += conduit->num_updated_local_records;
		if (conduit->num_updated_local_records == -1) {
			conduit->num_updated_local_records = conduit->total_records;
		}
		conduit->total_progress += conduit->num_updated_local_records;
		if (conduit->num_new_local_records == -1) {
			conduit->num_new_local_records = conduit->total_records;
		}
		conduit->total_progress += conduit->num_deleted_local_records;
		if (conduit->num_deleted_local_records == -1) {
			conduit->num_deleted_local_records = conduit->total_records;
		}
		conduit->total_progress += conduit->num_deleted_local_records;

		/* Decide on fast sync or slow sync */
		/* I think the following if is bad juju */
		if (1 || PI_DBINFO (dbinfo)->backupDate < PI_DBINFO (dbinfo)->modifyDate) {
			if (conduit_standard->slow==FALSE && gpilot_sync_pc_match (dbinfo)==TRUE) {
				result = FastSync(dbinfo->pilot_socket, dbinfo->db_handle, conduit);
			} else {
				conduit->total_progress += conduit->total_records;
				result = SlowSync(dbinfo->pilot_socket, dbinfo->db_handle, conduit);
			}
		}
		
		/* now disable the slow=true setting, so it doens't pass on to next time */
		if (conduit_standard->slow==TRUE)
			conduit_standard->slow = FALSE;
		
		standard_abs_merge_to_remote(conduit, dbinfo->pilot_socket, dbinfo->db_handle, SyncBothWays );
		standard_abs_check_locally_deleted_records(conduit, dbinfo->pilot_socket, 
							   dbinfo->db_handle, SyncBothWays);
	} while (0);

	Finish;
}

gint
gnome_pilot_conduit_standard_abs_match_record (GnomePilotConduitStandardAbs *conduit,
					       LocalRecord **local,
					       PilotRecord *remote)
{
	gint retval;

	g_return_val_if_fail (conduit != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD_ABS (conduit), -1);

	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_standard_abs_signals [MATCH_RECORD],
			 0,
			 local,
			 remote,
			 &retval);
	return retval;
}

gint
gnome_pilot_conduit_standard_abs_free_match (GnomePilotConduitStandardAbs *conduit,
					     LocalRecord **local)
{
	gint retval;

	g_return_val_if_fail (conduit != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD_ABS (conduit), -1);


	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_standard_abs_signals [FREE_MATCH],
			 0,
			 local,
			 &retval);
	return retval;
}

gint
gnome_pilot_conduit_standard_abs_archive_local (GnomePilotConduitStandardAbs *conduit,
						LocalRecord *local)
{
	gint retval;

	g_return_val_if_fail (conduit != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD_ABS (conduit), -1);

	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_standard_abs_signals [ARCHIVE_LOCAL],
			 0,
			 local,
			 &retval);
	return retval;
}

gint
gnome_pilot_conduit_standard_abs_archive_remote (GnomePilotConduitStandardAbs *conduit,
						 LocalRecord *local,
						 PilotRecord *remote)
{
	gint retval;

	g_return_val_if_fail (conduit != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD_ABS (conduit), -1);

	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_standard_abs_signals [ARCHIVE_REMOTE],
			 0,
			 local,
			 remote,
			 &retval);
	return retval;
}

gint
gnome_pilot_conduit_standard_abs_store_remote (GnomePilotConduitStandardAbs *conduit,
					       PilotRecord *remote)
{
	gint retval;

	g_return_val_if_fail (conduit != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD_ABS (conduit), -1);

	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_standard_abs_signals [STORE_REMOTE],
			 0,
			 remote,
			 &retval);
	return retval;
}


gint
gnome_pilot_conduit_standard_abs_iterate (GnomePilotConduitStandardAbs *conduit,
					  LocalRecord **local)
{
	gint retval;

	g_return_val_if_fail (conduit != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD_ABS (conduit), -1);

	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_standard_abs_signals [ITERATE],
			 0,
			 local,
			 &retval);
	return retval;
}

gint
gnome_pilot_conduit_standard_abs_iterate_specific (GnomePilotConduitStandardAbs *conduit,
						   LocalRecord **local,
						   gint flag,
						   gint archived)
{
	gint retval;

	g_return_val_if_fail (conduit != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD_ABS (conduit), -1);

	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_standard_abs_signals [ITERATE_SPECIFIC],
			 0,
			 local,
			 flag,
			 archived,
			 &retval);
	return retval;
}

gint
gnome_pilot_conduit_standard_abs_purge (GnomePilotConduitStandardAbs *conduit)
{
	gint retval;

	g_return_val_if_fail (conduit != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD_ABS (conduit), -1);

	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_standard_abs_signals [PURGE],
			 0,
			 &retval);
	return retval;
}

gint
gnome_pilot_conduit_standard_abs_set_status (GnomePilotConduitStandardAbs *conduit,
					     LocalRecord *local,
					     gint status)
{
	gint retval;

	g_return_val_if_fail (conduit != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD_ABS (conduit), -1);

	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_standard_abs_signals [SET_STATUS],
			 0,
			 local,
			 status,
			 &retval);
	return retval;
}


gint
gnome_pilot_conduit_standard_abs_set_pilot_id (GnomePilotConduitStandardAbs *conduit,
					       LocalRecord *local,
					       guint32 id)
{
	gint retval;

	g_return_val_if_fail (conduit != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD_ABS (conduit), -1);

	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_standard_abs_signals [SET_PILOT_ID],
			 0,
			 local,
			 id,
			 &retval);
	return retval;
}

gint
gnome_pilot_conduit_standard_abs_compare (GnomePilotConduitStandardAbs *conduit,
					  LocalRecord *local,
					  PilotRecord *remote)
{
	gint retval;

	g_return_val_if_fail (conduit != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD_ABS (conduit), -1);

	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_standard_abs_signals [COMPARE],
			 0,
			 local,
			 remote,
			 &retval);
	return retval;
}

gint
gnome_pilot_conduit_standard_abs_compare_backup (GnomePilotConduitStandardAbs *conduit,
						 LocalRecord *local,
						 PilotRecord *remote)
{
	gint retval;

	g_return_val_if_fail (conduit != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD_ABS (conduit), -1);

	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_standard_abs_signals [COMPARE_BACKUP],
			 0,
			 local,
			 remote,
			 &retval);
	return retval;
}

gint
gnome_pilot_conduit_standard_abs_free_transmit (GnomePilotConduitStandardAbs *conduit,
						LocalRecord *local,
						PilotRecord **remote)
{
	gint retval;

	g_return_val_if_fail (conduit != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD_ABS (conduit), -1);

	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_standard_abs_signals [FREE_TRANSMIT],
			 0,
			 local,
			 remote,
			 &retval);
	return retval;

}

gint
gnome_pilot_conduit_standard_abs_delete_all (GnomePilotConduitStandardAbs *conduit)
{
	gint retval;

	g_return_val_if_fail (conduit != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD_ABS (conduit), -1);

	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_standard_abs_signals [DELETE_ALL],
			 0,
			 &retval);
	return retval;
}

gint
gnome_pilot_conduit_standard_abs_transmit (GnomePilotConduitStandardAbs *conduit,
					   LocalRecord *local,
					   PilotRecord **remote)
{
	gint retval;

	g_return_val_if_fail (conduit != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD_ABS (conduit), -1);

	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_standard_abs_signals [TRANSMIT],
			 0,
			 local,
			 remote,
			 &retval);
	return retval;
}

gint
gnome_pilot_conduit_standard_abs_pre_sync (GnomePilotConduitStandardAbs *conduit,
					   GnomePilotDBInfo  *dbinfo)
{
	gint retval;

	g_return_val_if_fail (conduit != NULL, -1);
	g_return_val_if_fail (dbinfo != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD_ABS (conduit), -1);

	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_standard_abs_signals [PRE_SYNC],
			 0,
			 dbinfo,
			 &retval);
	return retval;
}

void
gnome_pilot_conduit_standard_abs_set_num_local_records (GnomePilotConduitStandardAbs *conduit,
							gint num) {
	g_return_if_fail (conduit != NULL);
	g_return_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD_ABS (conduit));
	conduit->num_local_records = num;
}

void
gnome_pilot_conduit_standard_abs_set_num_updated_local_records (GnomePilotConduitStandardAbs *conduit,
								gint num) {
	g_return_if_fail (conduit != NULL);
	g_return_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD_ABS (conduit));
       	conduit->num_updated_local_records = num;
}

void
gnome_pilot_conduit_standard_abs_set_num_new_local_records (GnomePilotConduitStandardAbs *conduit,
								gint num) {
	g_return_if_fail (conduit != NULL);
	g_return_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD_ABS (conduit));
	conduit->num_new_local_records = num;
}

void
gnome_pilot_conduit_standard_abs_set_num_deleted_local_records (GnomePilotConduitStandardAbs *conduit,
								gint num) {
	g_return_if_fail (conduit != NULL);
	g_return_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD_ABS (conduit));
	conduit->num_deleted_local_records = num;
}

void 
gnome_pilot_conduit_standard_abs_set_db_open_mode (GnomePilotConduitStandardAbs *conduit,
						   gint mode) {
	g_return_if_fail (conduit != NULL);
	g_return_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD_ABS (conduit));
	
	conduit->db_open_mode = mode;
}

/* Deletes a remote record from the pilot */
static void
standard_abs_delete_from_pilot(GnomePilotConduitStandardAbs *conduit,
			   int handle, 
			   int db, 
			   PilotRecord *remote) 
{
	int err;
	
	g_message("gpilotd: deleting record %ld from pilot",remote->ID);
	err = dlp_DeleteRecord(handle,db,0,remote->ID);
	if (err<0) {
		g_warning("dlp_DeleteRecord returned %d",err);
	}
	/* skip this records during later sync_record call */
	conduit->record_ids_to_ignore = g_slist_prepend(conduit->record_ids_to_ignore,
							GINT_TO_POINTER(remote->ID));
}

/* updates or adds a local record to the pilot. Note, that after calling this,
   the record has the dirty bit set.
   FIXME: Remember these id's and ignore during SyncRecord
   Use record_ids_to_ignore, add the recordId to this list, then let sync_record
   check the id of the record to sync as early as possible, and abort if match 
*/
static recordid_t
standard_abs_add_to_pilot (GnomePilotConduitStandardAbs *conduit,
			   int handle, 
			   int db, 
			   LocalRecord *local) 
{
	PilotRecord *remote;
	recordid_t assigned_id;
	int err;

	g_message("gpilotd: adding record to pilot");

	err = gnome_pilot_conduit_standard_abs_transmit (conduit, local, &remote);
	if (err < 0 || remote==NULL) {
	  g_warning(_("Conduit did not return a record"));
	}
	gnome_pilot_conduit_standard_abs_set_status (conduit, local, GnomePilotRecordNothing);

	err = dlp_WriteRecord (handle, db,
			       remote->secret?dlpRecAttrSecret:0,
			       remote->ID,
			       remote->category,
			       remote->record,
			       remote->length,
			       &assigned_id);
	if (err<0) {
		g_warning("dlp_WriteRecord returned %d",err);
		return 0;
	}

	conduit->record_ids_to_ignore = g_slist_prepend(conduit->record_ids_to_ignore,
						     GINT_TO_POINTER(assigned_id));
	gnome_pilot_conduit_standard_abs_free_transmit (conduit, local, &remote);
	
	return assigned_id;
}

/*
  This is messy, but its my first attempt at implementing the
  algoritm of conduit.pdf page 39-40 (see below). And yes, I'll try to collapse
  the methods when I'm sure which are to do the same stuff
  I've marked which of the possibilities from page 39-40 each case
  correlates to. case e1 is where neither local nor remote is modified.
  Basically, given a pilot-side, pc-side or both, using the flags
  it will determine how to synchronize the two sides.
  If either remote or local is NULL, it will retrieve the record.

  Also check http://palm.3com.com/devzone/docs/30wincdk/conduitc.htm#627041

  direction decides which operations may be called
     SyncToRemote - only operations that alter remote (meaning AddToPilot, dlp...)
     SyncToLocal  - only operations that alter local (meaning gnome_pilot_conduit_...)

  Call with SyncToRemote|SyncToLocal to synchronize both ways


Following is copied from conduit.pdf:39-40

Case #   Pilot State   PC State      Action 
----------------------------------------------------------------
1        Add           No Record     Add the Pilot record to the PC.

2        No Record     Add           Add the PC record to Pilot.

3        Delete	       No Modify     Delete the record on Pilot and the PC.

4        No Modify     Delete        Delete the record on Pilot and the PC.

5        Delete        Modify        Instead of deleting the Pilot record, replace the Pilot record 
                                     with the PC record. Message is sent to the log. 

6        Modify	       Delete        Instead of deleting the PC record, replace the PC record with 
                                     the Pilot record. Message is sent to the log.

7        Modify	       No Modify     Replace the PC record with the Pilot record. 

8        No Modify     Modify        Replace the Pilot record with the PC record.

9        Modify        Modify        If changes are identical, no action is taken.

10       Modify	       Modify        If changes are different, add the Pilot record to the PC, and 
                                     add the PC record to Pilot. Message is sent to the log.

11       Archive       No Record/    Archive the Pilot record. If the PC record exists, delete it. 
                       No Modify

12       Archive       Delete        Archive the Pilot record. Delete the PC record.

13       Archive with  Modify        Instead of archiving the Pilot record, replace the Pilot record 
         No Modify                   with the PC record. Message is sent to the log.

14       Archive       Modify	     If the records are identical, archive the Pilot record and delete 
         after Modify	             the record from the PC.

15       Archive       Modify	     If changes are different, do not archive the Pilot record. Add 
         after Modify	             the Pilot record to the PC and add the PC record to Pilot. 
	                             Message is sent to the log.

16       No Record/    Archive	     Archive the PC record. If the Pilot record exists, delete it.
         No Modify

17       Delete	       Archive       Archive the PC record. Delete the Pilot record.

18       Modify	       Archive with  Instead of archiving the PC record, replace the PC record 
                       No Modify     with the Pilot record. Message is sent to the log.

19       Modify	       Archive       If the records are identical, archive the PC record and delete 
                       after Modify  the record from Pilot.

20       Modify	       Archive       If changes are different, do not archive the PC record. Add 
                       after Modify  the Pilot record to the PC, and add the PC record to Pilot. 
                                     Message is sent to the log. 

	
 */

int
standard_abs_sync_record (GnomePilotConduitStandardAbs *conduit,
			  int handle,
			  int db,
			  LocalRecord *local,
			  PilotRecord *remote,
			  StandardAbsSyncDirection direction)
{
	gboolean free_remote;
	gboolean free_match;

	g_assert(conduit!=NULL);
	g_assert(!(local==NULL && remote==NULL));

	free_remote = FALSE;
	free_match = FALSE;

	if (local==NULL && remote!=NULL) {
		if (g_slist_find(conduit->record_ids_to_ignore,GINT_TO_POINTER(remote->ID))!=NULL) {
			g_message("gpilotd: this record has already been processed");
			return 0;
		}
       		gnome_pilot_conduit_standard_abs_match_record (conduit, &local, remote);
		free_match = TRUE;
	} else if (remote==NULL && local!=NULL) {
		int index;
		if (g_slist_find(conduit->record_ids_to_ignore,GINT_TO_POINTER(local->ID))!=NULL) {
			g_message("gpilotd: this record has already been processed");
			return 0;
		}

		g_message("gpilotd: retrieve %ld from pilot",local->ID);
		remote = g_new0(PilotRecord,1);
		remote->record = malloc(0xffff);
		gnome_pilot_compat_with_pilot_link_0_11_dlp_ReadRecordById(handle,db,
				   local->ID,
				   remote->record,
				   &index,
				   &remote->length,
				   &remote->attr,
				   &remote->category);
		remote->ID = local->ID;
		standard_abs_compute_attr_field(remote);
		free_remote = TRUE;
	} else if (remote==NULL && local==NULL) {
		g_error("SyncRecord called with two NULL parameters");
		return 0;
	} 
		

	if (local) {
		/* local record exists */
		if (remote->archived) {
			switch(local->attr) {
			case GnomePilotRecordModified: 
				if (remote->attr == GnomePilotRecordModified) {
					if (gnome_pilot_conduit_standard_abs_compare(conduit,local,remote) != 0) {
					 	/* CASE 15 */	
						g_message("gpilotd: sync_record: case 15");
						if ( direction & SyncToRemote ) {
							standard_abs_add_to_pilot (conduit, handle, db, local);
						}
						if ( direction & SyncToLocal ) {
							gnome_pilot_conduit_standard_abs_store_remote (conduit, remote);
						}
						dlp_AddSyncLogEntry (handle, LOG_CASE15);
						gnome_pilot_conduit_send_message(GNOME_PILOT_CONDUIT(conduit), 
										 LOG_CASE15);
					} else {
						/* CASE 14 */
						g_message("gpilotd: sync_record: case 14");
						remote->attr = GnomePilotRecordNothing;
						remote->archived = 0;
						if ( direction & SyncToLocal )
							gnome_pilot_conduit_standard_abs_archive_remote (conduit,
													 local, 
													 remote);
						if ( direction & SyncToRemote )
							standard_abs_delete_from_pilot(conduit,handle,db,remote);
					}
				} else {
					/* CASE 13 */
					g_message("gpilotd: sync_record: case 13");
					if ( direction & SyncToRemote ) {
						standard_abs_add_to_pilot (conduit, handle, db, local);
						dlp_AddSyncLogEntry (handle, LOG_CASE13);
						gnome_pilot_conduit_send_message(GNOME_PILOT_CONDUIT(conduit), 
										 LOG_CASE13);
					}
				}
				break;
			case GnomePilotRecordNothing:
				/* CASE 11 No Modify */
				g_message("gpilotd: sync_record: case 11 No Modify");
				remote->attr = GnomePilotRecordNothing;
				remote->archived = 0;
				if ( direction & SyncToLocal )
					gnome_pilot_conduit_standard_abs_archive_remote (conduit, local, remote);
				break;
			case GnomePilotRecordDeleted:
				/* CASE 12  */
				g_message("gpilotd: sync_record: case 12");
				remote->attr = GnomePilotRecordNothing;
				remote->archived = 0;
				if ( direction & SyncToLocal )
					gnome_pilot_conduit_standard_abs_archive_remote (conduit, local, remote);
				break;
			}
		} else {
			/* remote is not archived */
			switch (remote->attr) {
			case GnomePilotRecordModified:
				switch(local->attr) {
				case GnomePilotRecordModified:
					if (gnome_pilot_conduit_standard_abs_compare (conduit, local, remote) != 0) {
						if (local->archived) {
							/* CASE 20 */
							g_message("gpilotd: sync_record: case 20");
							if ( direction & SyncToLocal )
								gnome_pilot_conduit_standard_abs_store_remote (conduit, remote);
							if ( direction & SyncToRemote )
								standard_abs_add_to_pilot (conduit, handle, db, local);
							dlp_AddSyncLogEntry (handle, LOG_CASE20);
							gnome_pilot_conduit_send_message (GNOME_PILOT_CONDUIT(conduit), LOG_CASE20);
						} else {
							/* CASE 10 */
							g_message("gpilotd: sync_record: case 10");
							if ( direction & SyncToRemote ) {
								standard_abs_add_to_pilot (conduit, handle, db, local);
							}
							if ( direction & SyncToLocal ) {
								gnome_pilot_conduit_standard_abs_store_remote (conduit, remote);
							}
							dlp_AddSyncLogEntry (handle, LOG_CASE10);
							gnome_pilot_conduit_send_message (GNOME_PILOT_CONDUIT(conduit), LOG_CASE10);
						}
					} else {
						if (local->archived) {
							/* CASE 19 */
							g_message("gpilotd: sync_record: case 19");
							if ( direction & SyncToLocal )
								gnome_pilot_conduit_standard_abs_archive_local (conduit, local);
							if ( direction & SyncToRemote )
								standard_abs_delete_from_pilot(conduit,handle,db,remote);
						} else {
							/* CASE 9 */
							g_message("gpilotd: sync_record: case 9");
							if ( direction & SyncToLocal )
								gnome_pilot_conduit_standard_abs_set_status (conduit, local, GnomePilotRecordNothing);
						}
					}
					break;
				case GnomePilotRecordNothing:
					if(local->archived) {
						/* CASE 18 */
						g_message("gpilotd: sync_record: case 18");
						if ( direction & SyncToLocal ) {
							gnome_pilot_conduit_standard_abs_store_remote (conduit, remote);
							/* FIXME: should this be loged on pilot? */
							dlp_AddSyncLogEntry (handle, LOG_CASE18);
							gnome_pilot_conduit_send_message (GNOME_PILOT_CONDUIT(conduit), LOG_CASE18);
						}
					} else {
						/* CASE 7*/
						g_message("gpilotd: sync_record: case 7");
						if ( direction & SyncToLocal ) 
							gnome_pilot_conduit_standard_abs_store_remote (conduit, remote);
					}
					break;
				case GnomePilotRecordDeleted:
					/* CASE 6 */
					g_message("gpilotd: sync_record: case 6");
					if ( direction & SyncToLocal )
					{
						gnome_pilot_conduit_standard_abs_store_remote (conduit, remote);
						dlp_AddSyncLogEntry(handle, LOG_CASE6);
						gnome_pilot_conduit_send_message (GNOME_PILOT_CONDUIT(conduit), LOG_CASE6);
					}
					break;
				default:
					g_warning("gpilotd: sync_record: Unhandled sync case (b) Remote.attr = %d, Local.attr = %d\n",
						remote->attr, local->attr);
					break;
				}
				break;
			case GnomePilotRecordDeleted:
				if(local->archived) {
					/* CASE 17 */
					g_message("gpilotd: sync_record: case 17");
					if ( direction & SyncToLocal )
						gnome_pilot_conduit_standard_abs_archive_local (conduit, local);
					if ( direction & SyncToRemote )
						standard_abs_delete_from_pilot(conduit,handle,db,remote);
				} else {

					switch (local->attr) {
					case GnomePilotRecordModified:
						/* CASE 5 */
						g_message("gpilotd: sync_record: case 5");
						if ( direction & SyncToRemote ) {
							standard_abs_add_to_pilot (conduit, handle, db, local);
							dlp_AddSyncLogEntry(handle, LOG_CASE5);
							gnome_pilot_conduit_send_message (GNOME_PILOT_CONDUIT(conduit), LOG_CASE5);
						}
						break;
					case GnomePilotRecordDeleted:
						
						break;
					case GnomePilotRecordNothing:
						/* CASE 3 */
						g_message("gpilotd: sync_record: case 3");
						/* can be collapsed with case GnomePilotRecordDeleted */
						if ( direction & SyncToRemote ) {
							standard_abs_delete_from_pilot(conduit,handle,db,remote);
						}
						if ( direction & SyncToLocal ) {
							gnome_pilot_conduit_standard_abs_set_status (conduit, 
												     local, 
												     GnomePilotRecordDeleted);	
						}
						break;
					default:
						g_warning("gpilotd: sync_record: Unhandled sync case (a) Remote.attr = %d, local.attr = %d\n",
							  remote->attr, local->attr);
						break;
					}
				}
				break;
			case GnomePilotRecordNothing:
				if (local->archived) {
					/* CASE 16 */
					g_message("gpilotd: sync_record: case 16");
					if ( direction & SyncToLocal )
						gnome_pilot_conduit_standard_abs_archive_local (conduit, local);
					if ( direction & SyncToRemote )
						standard_abs_delete_from_pilot(conduit,handle,db,remote);
			} else {
					switch (local->attr) {
					case GnomePilotRecordDeleted:
						/* CASE 4 */
						g_message("gpilotd: sync_record: case 4, deleting record %ld",remote->ID);
						if ( direction & SyncToRemote )
							standard_abs_delete_from_pilot(conduit,handle,db,remote);
						if ( direction & SyncToLocal )
							gnome_pilot_conduit_standard_abs_set_status (conduit, local, GnomePilotRecordDeleted);
						break;
					case GnomePilotRecordModified: {
						/* CASE 8 */
						g_message("gpilotd: sync_record: case 8");
						if ( direction & SyncToRemote ) 
							standard_abs_add_to_pilot (conduit, handle, db, local);
					}
					break;
					case GnomePilotRecordNothing:
						/* CASE e1 */
						/* g_message("gpilotd: sync_record: case e1"); */
						break;
					default:
						g_warning("gpilotd: sync_record: Unhandled sync case (c) Remote.attr = %d, Local.attr = %d\n",
							remote->attr, local->attr);
						break;
					}
				}
				break;
			}
		}
		if (free_match)
			gnome_pilot_conduit_standard_abs_free_match (conduit, &local);
		if (free_remote) {
			free(remote->record);
			g_free(remote);
		}
	} else {
		/* no local record exists */
		if (remote->archived) {
			/* CASE 11 No Record */
			g_message("gpilotd: sync_record: case 11 No Record");
			remote->attr = GnomePilotRecordNothing;
			remote->archived = 0;
			if ( direction & SyncToLocal )
				gnome_pilot_conduit_standard_abs_archive_remote (conduit, 
										 NULL, 
										 remote);
		} else {
			/* CASE 1 */
			g_message("gpilotd: sync_record: case 1");
                        /* maybe it'd be nice if StoreRemote returned a localRecord ? */
			if ( direction & SyncToLocal ) {
				gnome_pilot_conduit_standard_abs_store_remote (conduit, 
									       remote); 
				gnome_pilot_conduit_standard_abs_match_record (conduit, 
									       &local, 
									       remote);
				if (local) {
					gnome_pilot_conduit_standard_abs_set_status (conduit, 
										     local, 
										     GnomePilotRecordNothing);
					gnome_pilot_conduit_standard_abs_free_match (conduit, &local);
				} else {
					g_warning(_("Error in conduit, newly added record could not be found"));
				}
			}
		}
	}
	/* CASE 2 handled by standard_abs_merge_to_remote */
	return 0;
}

gint 
standard_abs_check_locally_deleted_records(GnomePilotConduitStandardAbs *conduit, 
					   int handle, 
					   int db,
					   StandardAbsSyncDirection direction)
{	
	LocalRecord *local;
	local = NULL;

	g_assert(conduit!=NULL);
	/*
	  Have to iterate over the locally deleted records as well to delete them from the pilot.
	  This isn't done in the merge calls, since they merge, not synchronize.
	*/
	while(gnome_pilot_conduit_standard_abs_iterate_specific (conduit, &local, GnomePilotRecordDeleted, 0)) {
		g_message("gpilotd: locally deleted record...");
		standard_abs_sync_record(conduit, handle, db, local, NULL, direction);
		gnome_pilot_conduit_send_progress(GNOME_PILOT_CONDUIT(conduit),
						  conduit->total_progress, 
						  ++conduit->progress);
	}
	return 0;
}


gint
standard_abs_merge_to_remote (GnomePilotConduitStandardAbs *conduit,
			      int handle, 
			      int db, 			 
			      StandardAbsSyncDirection direction) 
{
	LocalRecord *local;

	local = NULL;

	g_assert(conduit!=NULL);

	/* First iterate over new records, install them. 
	   After installing, set attr to Nothing */
	if ( direction & SyncToRemote ) 
		while(gnome_pilot_conduit_standard_abs_iterate_specific (conduit, &local, GnomePilotRecordNew, 0)) {
			recordid_t assigned_id;
			/* FIXME: remote may need to be deleted first */
			assigned_id = standard_abs_add_to_pilot (conduit, handle, db, local);
			gnome_pilot_conduit_standard_abs_set_pilot_id (conduit, 
								       local, 
								       assigned_id);
			gnome_pilot_conduit_send_progress(GNOME_PILOT_CONDUIT(conduit),
							  conduit->total_progress, 
							  ++conduit->progress);
		}

	/* 
	   then iterate over modified records and sync them, this gets
	   the last records which FastSync and SlowSync didn't sync, since
	   they iterate over pilot-modified records
	*/
	while(gnome_pilot_conduit_standard_abs_iterate_specific (conduit, &local, GnomePilotRecordModified, 0)) {
		standard_abs_sync_record(conduit, handle, db, local, NULL, direction);
		gnome_pilot_conduit_send_progress(GNOME_PILOT_CONDUIT(conduit),
						  conduit->total_progress,
						  ++conduit->progress);
	}

	return 0;
}

gint
standard_abs_merge_to_local (GnomePilotConduitStandardAbs *conduit,
			     int handle, 
			     int db, 			     
			     StandardAbsSyncDirection direction) {
        int index;
	int retval;
	PilotRecord remote;
	unsigned char buffer[0xffff];
	
	g_assert(conduit!=NULL);

	index = 0;
	retval = 0;
	remote.record = buffer;

	while (gnome_pilot_compat_with_pilot_link_0_11_dlp_ReadRecordByIndex (handle, db,
				     index,
				     remote.record,
				     &remote.ID,
				     &remote.length,
				     &remote.attr,
				     &remote.category) >= 0) {
		standard_abs_compute_attr_field(&remote);
		if ( remote.attr == GnomePilotRecordNew ||
		     remote.attr == GnomePilotRecordModified)
			standard_abs_sync_record (conduit, handle, db, NULL, &remote, direction);
		index++;
		gnome_pilot_conduit_send_progress(GNOME_PILOT_CONDUIT(conduit),
						  conduit->total_progress,
						  index);
	}
	conduit->progress = index;

	return 0;
}

int
SlowSync (int handle, 
	  int db, 
	  GnomePilotConduitStandardAbs *conduit) 
{
        int index;
        int retval;
	PilotRecord remote;
	unsigned char buffer[0xffff];

	g_assert(conduit!=NULL);

	index = 0;
	retval = 0;
	remote.record = buffer;

	g_message("Performing Slow Synchronization");

	while (gnome_pilot_compat_with_pilot_link_0_11_dlp_ReadRecordByIndex (handle, db,
				     index,
				     remote.record,
				     &remote.ID,
				     &remote.length,
				     &remote.attr,
				     &remote.category) >= 0) {
		standard_abs_compute_attr_field(&remote);
		standard_abs_sync_record (conduit, handle, db, NULL, &remote, SyncBothWays);
		index++;
		gnome_pilot_conduit_send_progress(GNOME_PILOT_CONDUIT(conduit),
						  conduit->total_progress,
						  index);
	}
	conduit->progress = index;

	return retval;
}

/* Perform a "fast" sync. This requires that both the remote (Pilot) and
   local (PC) have consistent, accurate, and sufficient modification flags.
   If this is not true, a slow sync should be used */
gint
FastSync (int handle, 
	  int db, 
	  GnomePilotConduitStandardAbs *conduit) 
{
        int index;
	int retval;
	unsigned char buffer[0xffff];
	PilotRecord remote;

      	g_assert(conduit!=NULL);

	index = 0;
	retval = 0;
	remote.record = buffer;

	g_message("Performing Fast Synchronization");

	while (gnome_pilot_compat_with_pilot_link_0_11_dlp_ReadNextModifiedRec (handle, db,
					remote.record,
					&remote.ID,
					&index,
					&remote.length,
					&remote.attr,
					&remote.category) >= 0) {
		standard_abs_compute_attr_field(&remote);
		standard_abs_sync_record (conduit, handle, db, NULL, &remote, SyncBothWays);
		gnome_pilot_conduit_send_progress(GNOME_PILOT_CONDUIT(conduit),
						  conduit->total_progress,
						  index);
	}
	conduit->progress = index;

	return retval;
}

gboolean
gpilot_sync_pc_match(GnomePilotDBInfo *dbinfo)
{
	GnomePilotSyncStamp *stamp;

	stamp=(GnomePilotSyncStamp *)dbinfo->manager_data;

	if (stamp->sync_PC_Id == dbinfo->pu->lastSyncPC) {
		return TRUE;
	}
	return FALSE;
}

gint
standard_abs_open_db(GnomePilotConduitStandardAbs *conduit,
		     GnomePilotDBInfo *dbinfo)
{
	gchar *name;
	int err;
  
	g_assert(conduit!=NULL);
	g_assert(dbinfo!=NULL);

	name = g_strdup(gnome_pilot_conduit_standard_get_db_name (GNOME_PILOT_CONDUIT_STANDARD(conduit)));
	if (conduit->db_open_mode!=0) {
		g_message("gpilotd: open_db: opening with %d\n",conduit->db_open_mode);
		err = dlp_OpenDB (dbinfo->pilot_socket, 0, conduit->db_open_mode, 
				  name, 
				  &(dbinfo->db_handle));
	} else {
		err = dlp_OpenDB (dbinfo->pilot_socket, 0, dlpOpenReadWrite,
				  name,
				  &(dbinfo->db_handle));
	}

	if (err < 0) {
		g_message ("gpilotd: open_db, error %s", dlp_strerror (err));
	} else {		
		dlp_ReadOpenDBInfo(dbinfo->pilot_socket,dbinfo->db_handle,&conduit->total_records);
	}
	g_free(name);

	return err;
}

void
standard_abs_close_db_and_purge_local(GnomePilotConduitStandardAbs *conduit,
				      GnomePilotDBInfo *dbinfo,
				      gboolean clean)
{
	g_assert(conduit!=NULL);
	g_assert(dbinfo!=NULL);

	if (clean) {
		dlp_CleanUpDatabase (dbinfo->pilot_socket, dbinfo->db_handle);
		gnome_pilot_conduit_standard_abs_purge (conduit);		
		dlp_ResetSyncFlags (dbinfo->pilot_socket, dbinfo->db_handle);
	}
	dlp_CloseDB (dbinfo->pilot_socket, dbinfo->db_handle);
}
