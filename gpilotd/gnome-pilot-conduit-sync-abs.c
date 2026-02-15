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
 *          JP Rosevear <jpr@arcavia.com>
 *
 */

#include <glib/gi18n.h>
#include <pi-source.h>
#include <pi-socket.h>
#include <pi-dlp.h>
#include <pi-sync.h>
#include "gpmarshal.h"
#include "gnome-pilot-conduit-sync-abs.h"
#include "manager.h"

enum {
	PRE_SYNC,
	POST_SYNC,
	SET_PILOT_ID,
	SET_STATUS_CLEARED,
	FOR_EACH,
	FOR_EACH_MODIFIED,
	COMPARE,
	ADD_RECORD,
	REPLACE_RECORD,
	DELETE_RECORD,
	ARCHIVE_RECORD,
	MATCH,
	FREE_MATCH,
	PREPARE,
	LAST_SIGNAL
};

typedef struct
{
	GnomePilotConduitSyncAbs *conduit;
	GnomePilotDBInfo *dbinfo;
} gp_closure;

/* Standard class methods */
static void gnome_pilot_conduit_sync_abs_init (GnomePilotConduitSyncAbs *pilot_conduit_sync_abs);
static void gnome_pilot_conduit_sync_abs_class_init (GnomePilotConduitSyncAbsClass *klass);

/* Virtual methods for the standard conduit class */
static gint gnome_pilot_conduit_standard_real_copy_to_pilot (GnomePilotConduitStandard *conduit,
							     GnomePilotDBInfo  *dbinfo);
static gint gnome_pilot_conduit_standard_real_copy_from_pilot (GnomePilotConduitStandard *conduit,
							       GnomePilotDBInfo  *dbinfo);
static gint gnome_pilot_conduit_standard_real_merge_to_pilot (GnomePilotConduitStandard *conduit,
							      GnomePilotDBInfo  *dbinfo);
static gint gnome_pilot_conduit_standard_real_merge_from_pilot (GnomePilotConduitStandard *conduit,
								GnomePilotDBInfo  *dbinfo);
static gint gnome_pilot_conduit_standard_real_synchronize (GnomePilotConduitStandard *conduit,
							   GnomePilotDBInfo  *dbinfo);

/* The sync handler callback functions */
static gint gnome_pilot_conduit_sync_abs_pre_sync (SyncHandler *sh, int dbhandle, int *slow);
static gint gnome_pilot_conduit_sync_abs_post_sync (SyncHandler *sh, int dbhandle);

static gint gnome_pilot_conduit_sync_abs_set_pilot_id (SyncHandler *sh, DesktopRecord *dr, recordid_t id);
static gint gnome_pilot_conduit_sync_abs_set_status_cleared (SyncHandler *sh, DesktopRecord *dr);

static gint gnome_pilot_conduit_sync_abs_for_each (SyncHandler *sh, DesktopRecord **dr);
static gint gnome_pilot_conduit_sync_abs_for_each_modified (SyncHandler *sh, DesktopRecord **dr);
static gint gnome_pilot_conduit_sync_abs_compare (SyncHandler *sh, PilotRecord *pr, DesktopRecord *dr);

static gint gnome_pilot_conduit_sync_abs_add_record (SyncHandler *sh, PilotRecord *pr);
static gint gnome_pilot_conduit_sync_abs_replace_record (SyncHandler *sh, DesktopRecord *dr, 
							 PilotRecord *pr);
static gint gnome_pilot_conduit_sync_abs_delete_record (SyncHandler *sh, DesktopRecord *dr);
static gint gnome_pilot_conduit_sync_abs_archive_record (SyncHandler *sh, DesktopRecord *pr, int archive);

static gint gnome_pilot_conduit_sync_abs_match (SyncHandler *sh, PilotRecord *pr, DesktopRecord **dr);
static gint gnome_pilot_conduit_sync_abs_free_match (SyncHandler *sh, DesktopRecord *dr);

static gint gnome_pilot_conduit_sync_abs_prepare (SyncHandler *sh, DesktopRecord *dr, PilotRecord *pr);

/* Local utility routines */
static gboolean gpilot_sync_pc_match (GnomePilotDBInfo *);

static GnomePilotConduitStandardClass *parent_class = NULL;
static guint pilot_conduit_sync_abs_signals[LAST_SIGNAL] = { 0 };

GType
gnome_pilot_conduit_sync_abs_get_type (void)
{
	static GType pilot_conduit_sync_abs_type = 0;

	if (!pilot_conduit_sync_abs_type)
	{
		static const GTypeInfo pilot_conduit_sync_abs_info =
		{
			sizeof (GnomePilotConduitSyncAbsClass),
			NULL,
			NULL,
			(GClassInitFunc) gnome_pilot_conduit_sync_abs_class_init,
			NULL,
			NULL,
			sizeof (GnomePilotConduitSyncAbs),
			0,
			(GInstanceInitFunc) gnome_pilot_conduit_sync_abs_init,
		};
		pilot_conduit_sync_abs_type = g_type_register_static (
		    gnome_pilot_conduit_standard_get_type (),
			"GnomePilotConduitSyncAbs", &pilot_conduit_sync_abs_info, 0);
	}

	return pilot_conduit_sync_abs_type;
}

static void
gnome_pilot_conduit_sync_abs_class_init (GnomePilotConduitSyncAbsClass *klass)
{
	GObjectClass *object_class;
	GnomePilotConduitStandardClass *conduit_standard_class;

	object_class = (GObjectClass*) klass;
	conduit_standard_class = (GnomePilotConduitStandardClass *) klass;

	parent_class = g_type_class_peek (gnome_pilot_conduit_standard_get_type ());

	pilot_conduit_sync_abs_signals[PRE_SYNC] =
		g_signal_new   ("pre_sync",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitSyncAbsClass, pre_sync),
				NULL,
				NULL,
				gp_marshal_INT__POINTER,
				G_TYPE_INT, 1, G_TYPE_POINTER);

	pilot_conduit_sync_abs_signals[POST_SYNC] =
		g_signal_new   ("post_sync",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitSyncAbsClass, post_sync),
				NULL,
				NULL,
				gp_marshal_INT__POINTER,
				G_TYPE_INT, 1, G_TYPE_POINTER);


	pilot_conduit_sync_abs_signals[SET_PILOT_ID] =
		g_signal_new   ("set_pilot_id",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitSyncAbsClass, set_pilot_id),
				NULL,
				NULL,
				gp_marshal_INT__POINTER_INT,
				G_TYPE_INT, 2, G_TYPE_POINTER, G_TYPE_INT);


	pilot_conduit_sync_abs_signals[SET_STATUS_CLEARED] =
		g_signal_new   ("set_status_cleared",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitSyncAbsClass, set_status_cleared),
				NULL,
				NULL,
				gp_marshal_INT__POINTER,
				G_TYPE_INT, 1, G_TYPE_POINTER);

	pilot_conduit_sync_abs_signals[FOR_EACH] =
		g_signal_new   ("for_each",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitSyncAbsClass, for_each),
				NULL,
				NULL,
				gp_marshal_INT__POINTER,
				G_TYPE_INT, 1, G_TYPE_POINTER);

	pilot_conduit_sync_abs_signals[FOR_EACH_MODIFIED] =
		g_signal_new   ("for_each_modified",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitSyncAbsClass, for_each_modified),
				NULL,
				NULL,
				gp_marshal_INT__POINTER,
				G_TYPE_INT, 1, G_TYPE_POINTER);

	pilot_conduit_sync_abs_signals[COMPARE] =
		g_signal_new   ("compare",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitSyncAbsClass, compare),
				NULL,
				NULL,
				gp_marshal_INT__POINTER_POINTER,
				G_TYPE_INT, 2, G_TYPE_POINTER, G_TYPE_POINTER);

	pilot_conduit_sync_abs_signals[ADD_RECORD] =
		g_signal_new   ("add_record",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitSyncAbsClass, add_record),
				NULL,
				NULL,
				gp_marshal_INT__POINTER,
				G_TYPE_INT, 1, G_TYPE_POINTER);

	pilot_conduit_sync_abs_signals[REPLACE_RECORD] =
		g_signal_new   ("replace_record",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitSyncAbsClass, replace_record),
				NULL,
				NULL,
				gp_marshal_INT__POINTER_POINTER,
				G_TYPE_INT, 2, G_TYPE_POINTER, G_TYPE_POINTER);

	pilot_conduit_sync_abs_signals[DELETE_RECORD] =
		g_signal_new   ("delete_record",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitSyncAbsClass, delete_record),
				NULL,
				NULL,
				gp_marshal_INT__POINTER,
				G_TYPE_INT, 1, G_TYPE_POINTER);

	pilot_conduit_sync_abs_signals[ARCHIVE_RECORD] =
		g_signal_new   ("archive_record",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitSyncAbsClass, archive_record),
				NULL,
				NULL,
				gp_marshal_INT__POINTER_BOOL,
				G_TYPE_INT, 2, G_TYPE_POINTER, G_TYPE_BOOLEAN);

	pilot_conduit_sync_abs_signals[MATCH] =
		g_signal_new   ("match",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitSyncAbsClass, match),
				NULL,
				NULL,
				gp_marshal_INT__POINTER_POINTER,
				G_TYPE_INT, 2, G_TYPE_POINTER, G_TYPE_POINTER);

	pilot_conduit_sync_abs_signals[FREE_MATCH] =
		g_signal_new   ("free_match",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitSyncAbsClass, free_match),
				NULL,
				NULL,
				gp_marshal_INT__POINTER,
				G_TYPE_INT, 1, G_TYPE_POINTER);

	pilot_conduit_sync_abs_signals[PREPARE] =
		g_signal_new   ("prepare",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GnomePilotConduitSyncAbsClass, prepare),
				NULL,
				NULL,
				gp_marshal_INT__POINTER_POINTER,
				G_TYPE_INT, 2, G_TYPE_POINTER, G_TYPE_POINTER);

	conduit_standard_class->copy_to_pilot = gnome_pilot_conduit_standard_real_copy_to_pilot;
	conduit_standard_class->copy_from_pilot = gnome_pilot_conduit_standard_real_copy_from_pilot;
	conduit_standard_class->merge_to_pilot = gnome_pilot_conduit_standard_real_merge_to_pilot;
	conduit_standard_class->merge_from_pilot = gnome_pilot_conduit_standard_real_merge_from_pilot;
	conduit_standard_class->synchronize = gnome_pilot_conduit_standard_real_synchronize;
}

static void
gnome_pilot_conduit_sync_abs_init (GnomePilotConduitSyncAbs *conduit)
{
	conduit->record_ids_to_ignore = NULL;
	conduit->total_records = 0;
	conduit->num_local_records = -1;
	conduit->num_updated_local_records = -1;
	conduit->num_new_local_records = -1;
	conduit->num_deleted_local_records = -1;
	conduit->progress = 0; /* as usual... */
	conduit->total_progress = 0; /* oh no!... */

}

GObject *
gnome_pilot_conduit_sync_abs_new (char *db_name,
				  guint32 creator_id)
{
	GnomePilotConduitSyncAbs *retval;

	retval =  GNOME_PILOT_CONDUIT_SYNC_ABS (g_object_new (gnome_pilot_conduit_sync_abs_get_type (),
								"GnomePilotConduitStandard::db_name", db_name,
								"GnomePilotConduitStandard::creator_id", creator_id,
								NULL));

	return G_OBJECT (retval);
}

/* Local utility routines */
static gboolean
gpilot_sync_pc_match(GnomePilotDBInfo *dbinfo)
{
	GnomePilotSyncStamp *stamp;

	stamp=(GnomePilotSyncStamp *)dbinfo->manager_data;

#if 0
	/* FIXME: these are for debug purposes */
	if (stamp->sync_PC_Id != dbinfo->pu->lastSyncPC) g_message("Cannot Fast Synchronize, SyncPC id failed");
	if (stamp->last_sync_date != dbinfo->pu->successfulSyncDate) g_message("Cannot Fast Synchronize, SyncDate failed");
#endif
/*
	if (stamp->sync_PC_Id == dbinfo->pu->lastSyncPC &&
	   stamp->last_sync_date==dbinfo->pu->successfulSyncDate) {
*/
	if (stamp->sync_PC_Id == dbinfo->pu->lastSyncPC) {
		return TRUE;
	}
	return FALSE;
}

static SyncHandler *
sync_abs_new_sync_handler (GnomePilotConduitSyncAbs *conduit,
			   GnomePilotDBInfo *dbinfo)
{
	SyncHandler *sh;
	gp_closure *gpc;
	GnomePilotConduitStandard *conduit_standard;
	
	conduit_standard = GNOME_PILOT_CONDUIT_STANDARD (conduit);
	
	sh = g_new0 (SyncHandler, 1);
	gpc = g_new0 (gp_closure, 1);

	sh->sd = dbinfo->pilot_socket;
	sh->name = g_strdup (gnome_pilot_conduit_standard_get_db_name (conduit_standard));

	gpc->conduit = conduit;
	gpc->dbinfo = dbinfo;
	sh->data = gpc;

	sh->Pre = gnome_pilot_conduit_sync_abs_pre_sync;
	sh->Post = gnome_pilot_conduit_sync_abs_post_sync;
	sh->SetPilotID = gnome_pilot_conduit_sync_abs_set_pilot_id;
	sh->SetStatusCleared = gnome_pilot_conduit_sync_abs_set_status_cleared;
	sh->ForEach = gnome_pilot_conduit_sync_abs_for_each;
	sh->ForEachModified = gnome_pilot_conduit_sync_abs_for_each_modified;
	sh->Compare = gnome_pilot_conduit_sync_abs_compare;
	sh->AddRecord = gnome_pilot_conduit_sync_abs_add_record;
	sh->ReplaceRecord = gnome_pilot_conduit_sync_abs_replace_record;
	sh->DeleteRecord = gnome_pilot_conduit_sync_abs_delete_record;
	sh->ArchiveRecord = gnome_pilot_conduit_sync_abs_archive_record;
	sh->Match = gnome_pilot_conduit_sync_abs_match;
	sh->FreeMatch = gnome_pilot_conduit_sync_abs_free_match;
	sh->Prepare = gnome_pilot_conduit_sync_abs_prepare;

	return sh;
}

static void
sync_abs_free_sync_handler (SyncHandler *sh)
{
	g_free (sh->name);
	g_free (sh->data);
	g_free (sh);
}

static GnomePilotRecord *
sync_abs_pr_to_gpr (PilotRecord *pr)
{
	GnomePilotRecord *gpr;
	
	gpr = g_new (GnomePilotRecord, 1);

	gpr->ID = pr->recID;
	gpr->category = pr->catID;
	gpr->record = pr->buffer;
	gpr->length = pr->len;

	gpr->secret = pr->flags & dlpRecAttrSecret ? TRUE : FALSE;
	gpr->archived = pr->flags & dlpRecAttrArchived ? TRUE : FALSE;

	if (pr->flags & dlpRecAttrDeleted)
		gpr->attr = GnomePilotRecordDeleted;
	else if (pr->flags & dlpRecAttrDirty)
		gpr->attr = GnomePilotRecordModified;
	else
		gpr->attr = GnomePilotRecordNothing;

	return gpr;
}

static PilotRecord
sync_abs_gpr_to_pr (GnomePilotRecord *gpr)
{
	PilotRecord pr;

	pr.recID = gpr->ID;
	pr.catID = gpr->category;
	pr.buffer = gpr->record;
	pr.len = gpr->length;

	pr.flags = 0;
	if (gpr->secret)
		pr.flags = pr.flags | dlpRecAttrSecret;
	if (gpr->archived)
		pr.flags = pr.flags | dlpRecAttrArchived;

	switch (gpr->attr) {
	case GnomePilotRecordNothing:
		break;
	case GnomePilotRecordNew:
	case GnomePilotRecordModified:
		pr.flags = pr.flags | dlpRecAttrDirty;
		break;
	case GnomePilotRecordDeleted:
		pr.flags = pr.flags | dlpRecAttrDeleted;
		break;
	}
	
	return pr;
	
}

static void
sync_abs_fill_gdr (GnomePilotDesktopRecord *gdr)
{
	DesktopRecord *dr;

	dr = &gdr->dr;
	
	gdr->ID = dr->recID;
	gdr->category = dr->catID;

	gdr->secret = dr->flags & dlpRecAttrSecret ? TRUE : FALSE;
	gdr->archived = dr->flags & dlpRecAttrArchived ? TRUE : FALSE;

	if (dr->flags & dlpRecAttrDeleted)
		gdr->attr = GnomePilotRecordDeleted;
	else if (dr->flags & dlpRecAttrDirty)
		gdr->attr = GnomePilotRecordModified;
	else
		gdr->attr = GnomePilotRecordNothing;
}

static void
sync_abs_fill_dr (GnomePilotDesktopRecord *gdr)
{
	DesktopRecord *dr;

	dr = &gdr->dr;
	
	dr->recID = gdr->ID;
	dr->catID = gdr->category;

	dr->flags = 0;
	if (gdr->secret)
		dr->flags = dr->flags | dlpRecAttrSecret;
	if (gdr->archived)
		dr->flags = dr->flags | dlpRecAttrArchived;

	switch (gdr->attr) {
	case GnomePilotRecordNothing:
		break;
	case GnomePilotRecordNew:
	case GnomePilotRecordModified:
		dr->flags = dr->flags | dlpRecAttrDirty;
		break;
	case GnomePilotRecordDeleted:
		dr->flags = dr->flags | dlpRecAttrDeleted;
		break;
	}
}

static gint
gnome_pilot_conduit_standard_real_copy_to_pilot (GnomePilotConduitStandard *conduit_standard,
						 GnomePilotDBInfo *dbinfo)
{
	GnomePilotConduitSyncAbs *conduit = NULL;
	SyncHandler *sh;

	g_return_val_if_fail (conduit_standard != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_SYNC_ABS (conduit_standard), -1);

	conduit = GNOME_PILOT_CONDUIT_SYNC_ABS (conduit_standard);

	sh = sync_abs_new_sync_handler (conduit, dbinfo);

	/* Set the counters for the progress bar */
	/* Total_records is set in the pre_sync callback */
	if (conduit->num_local_records == -1) {
		conduit->num_local_records = conduit->total_records;
	}
	conduit->total_progress += conduit->num_updated_local_records;

	if (sync_CopyToPilot (sh) != 0) {
		g_warning(_("Copy to PDA failed!"));
		return -1;
	}

	sync_abs_free_sync_handler (sh);
	
	return 0;
}

static gint
gnome_pilot_conduit_standard_real_copy_from_pilot (GnomePilotConduitStandard *conduit_standard,
						   GnomePilotDBInfo  *dbinfo)
{
	GnomePilotConduitSyncAbs *conduit = NULL;
	SyncHandler *sh;
	
	g_return_val_if_fail (conduit_standard != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_SYNC_ABS (conduit_standard), -1);

	conduit = GNOME_PILOT_CONDUIT_SYNC_ABS (conduit_standard);

	sh = sync_abs_new_sync_handler (conduit, dbinfo);

	if (sync_CopyFromPilot (sh) != 0) {
		g_warning(_("Copy from PDA failed!"));
		return -1;
	}

	sync_abs_free_sync_handler (sh);

	return 0;
}

static gint
gnome_pilot_conduit_standard_real_merge_to_pilot (GnomePilotConduitStandard *conduit_standard,
						  GnomePilotDBInfo  *dbinfo)
{
	GnomePilotConduitSyncAbs *conduit = NULL;
	SyncHandler *sh;
	
	g_return_val_if_fail (conduit_standard != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_SYNC_ABS (conduit_standard), -1);	

	conduit = GNOME_PILOT_CONDUIT_SYNC_ABS (conduit_standard);

	sh = sync_abs_new_sync_handler (conduit, dbinfo);

	if (sync_MergeToPilot (sh) != 0) {
		g_warning(_("Merge to PDA failed!"));
		return -1;
	}

	sync_abs_free_sync_handler (sh);

	return 0;
}

static gint
gnome_pilot_conduit_standard_real_merge_from_pilot (GnomePilotConduitStandard *conduit_standard,
						    GnomePilotDBInfo  *dbinfo)
{
	GnomePilotConduitSyncAbs *conduit = NULL;
	SyncHandler *sh;

	g_return_val_if_fail (conduit_standard != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_SYNC_ABS (conduit_standard), -1);

	conduit = GNOME_PILOT_CONDUIT_SYNC_ABS (conduit_standard);

	sh = sync_abs_new_sync_handler (conduit, dbinfo);

	if (sync_MergeFromPilot (sh) != 0) {
		g_warning(_("Merge from PDA failed!"));
		return -1;
	}

	sync_abs_free_sync_handler (sh);

	return 0;
}

static gint
gnome_pilot_conduit_standard_real_synchronize (GnomePilotConduitStandard *conduit_standard,
					       GnomePilotDBInfo  *dbinfo)
{
	GnomePilotConduitSyncAbs *conduit = NULL;
	SyncHandler *sh;
	int retval = 0;

	g_return_val_if_fail (conduit_standard != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_SYNC_ABS (conduit_standard), -1);

	conduit = GNOME_PILOT_CONDUIT_SYNC_ABS(conduit_standard);

	sh = sync_abs_new_sync_handler (conduit, dbinfo);

	/* Set the counters for the progress bar, note, total_records is set
	   in sync_abs_open_db */
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
		if (!(conduit_standard->slow==FALSE && gpilot_sync_pc_match (dbinfo)==TRUE)) {
			conduit_standard->slow = TRUE;
			conduit->total_progress += conduit->total_records;
		}
	}
	retval = sync_Synchronize (sh);

	if (retval != 0) {
		g_warning(_("Synchronization failed!"));
		return -1;
	}

	sync_abs_free_sync_handler (sh);

	/* now disable the slow=true setting, so it doesn't pass on to next time */
	if (conduit_standard->slow==TRUE)
		conduit_standard->slow = FALSE;

	return retval;
}

static gint
gnome_pilot_conduit_sync_abs_pre_sync (SyncHandler *sh, int dbhandle, int *slow)
{
	GnomePilotConduitSyncAbs *conduit;
	GnomePilotDBInfo *dbinfo;
	gint retval = 0;

	conduit = ((gp_closure *)sh->data)->conduit;
	dbinfo = ((gp_closure *)sh->data)->dbinfo;
	
	dbinfo->db_handle = dbhandle;
	
	dlp_ReadOpenDBInfo (dbinfo->pilot_socket, dbinfo->db_handle, &conduit->total_records);

	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_sync_abs_signals [PRE_SYNC],
			 0,
			 dbinfo,
			 &retval);

	*slow = GNOME_PILOT_CONDUIT_STANDARD (conduit)->slow ? 1 : 0;

	return retval;
}

static gint
gnome_pilot_conduit_sync_abs_post_sync (SyncHandler *sh, int dbhandle)
{
	GnomePilotConduitSyncAbs *conduit;
	GnomePilotDBInfo *dbinfo;
	gint retval = 0;

	conduit = ((gp_closure *)sh->data)->conduit;
	dbinfo = ((gp_closure *)sh->data)->dbinfo;

	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_sync_abs_signals [POST_SYNC],
			 0,
			 dbinfo,
			 &retval);
	return retval;
}

static gint
gnome_pilot_conduit_sync_abs_set_pilot_id (SyncHandler *sh, DesktopRecord *dr, recordid_t id)
{
	GnomePilotConduitSyncAbs *conduit;
	GnomePilotDesktopRecord *gdr;
	gint retval = 0;

	conduit = ((gp_closure *)sh->data)->conduit;
	gdr = (GnomePilotDesktopRecord *)dr;
	sync_abs_fill_gdr (gdr);
	
	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_sync_abs_signals [SET_PILOT_ID],
			 0,
			 gdr, id, &retval);
	
	sync_abs_fill_dr (gdr);
	
	return retval;
}

static gint
gnome_pilot_conduit_sync_abs_set_status_cleared (SyncHandler *sh, DesktopRecord *dr)
{
	GnomePilotConduitSyncAbs *conduit;
	GnomePilotDesktopRecord *gdr;
	gint retval = 0;

	conduit = ((gp_closure *)sh->data)->conduit;
	gdr = (GnomePilotDesktopRecord *)dr;
	sync_abs_fill_gdr (gdr);
	
	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_sync_abs_signals [SET_STATUS_CLEARED],
			 0,
			 gdr, &retval);
	
	sync_abs_fill_dr (gdr);
	
	return retval;
}

static gint
gnome_pilot_conduit_sync_abs_for_each (SyncHandler *sh, DesktopRecord **dr)
{
	GnomePilotConduitSyncAbs *conduit;
	GnomePilotDesktopRecord *gdr = (GnomePilotDesktopRecord *)*dr;
	gint retval = 0;

	conduit = ((gp_closure *)sh->data)->conduit;

	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_sync_abs_signals [FOR_EACH],
			 0,
			 &gdr, &retval);

	if (gdr != NULL)
		sync_abs_fill_dr (gdr);
	
	*dr = (DesktopRecord *)gdr;

	return retval;
}

static gint
gnome_pilot_conduit_sync_abs_for_each_modified (SyncHandler *sh, DesktopRecord **dr)
{
	GnomePilotConduitSyncAbs *conduit;
	GnomePilotDesktopRecord *gdr = (GnomePilotDesktopRecord *)*dr;
	gint retval = 0;

	conduit = ((gp_closure *)sh->data)->conduit;

	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_sync_abs_signals [FOR_EACH_MODIFIED],
			 0,
			 &gdr, &retval);

	if (gdr != NULL)
		sync_abs_fill_dr (gdr);

	*dr = (DesktopRecord *)gdr;

	return retval;
}

static gint
gnome_pilot_conduit_sync_abs_compare (SyncHandler *sh, PilotRecord *pr, DesktopRecord *dr)
{
	GnomePilotConduitSyncAbs *conduit;
	GnomePilotDesktopRecord *gdr;
	GnomePilotRecord *gpr;
	gint retval = 0;

	conduit = ((gp_closure *)sh->data)->conduit;
	gpr = sync_abs_pr_to_gpr (pr);
	gdr = (GnomePilotDesktopRecord *)dr;
	sync_abs_fill_gdr (gdr);

	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_sync_abs_signals [COMPARE],
			 0,
			 gdr, gpr, &retval);

	g_free (gpr);
	
	return retval;
}

static gint
gnome_pilot_conduit_sync_abs_add_record (SyncHandler *sh, PilotRecord *pr)
{
	GnomePilotConduitSyncAbs *conduit;
	GnomePilotRecord *gpr;
	gint retval = 0;

	conduit = ((gp_closure *)sh->data)->conduit;
	gpr = sync_abs_pr_to_gpr (pr);

	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_sync_abs_signals [ADD_RECORD],
			 0,
			 gpr, &retval);

	g_free (gpr);
	
	return retval;
}

static gint
gnome_pilot_conduit_sync_abs_replace_record (SyncHandler *sh, DesktopRecord *dr, PilotRecord *pr)
{
	GnomePilotConduitSyncAbs *conduit;
	GnomePilotDesktopRecord *gdr;
	GnomePilotRecord *gpr;
	gint retval = 0;

	conduit = ((gp_closure *)sh->data)->conduit;
	gdr = (GnomePilotDesktopRecord *)dr;
	sync_abs_fill_gdr (gdr);
	gpr = sync_abs_pr_to_gpr (pr);

	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_sync_abs_signals [REPLACE_RECORD],
			 0,
			 gdr, gpr, &retval);

	g_free (gpr);
	
	return retval;
}

static gint
gnome_pilot_conduit_sync_abs_delete_record (SyncHandler *sh, DesktopRecord *dr)
{
	GnomePilotConduitSyncAbs *conduit;
	GnomePilotDesktopRecord *gdr;
	gint retval = 0;

	conduit = ((gp_closure *)sh->data)->conduit;
	gdr = (GnomePilotDesktopRecord *)dr;
	sync_abs_fill_gdr (gdr);

	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_sync_abs_signals [DELETE_RECORD],
			 0,
			 gdr, &retval);

	return retval;
}

static gint
gnome_pilot_conduit_sync_abs_archive_record (SyncHandler *sh, DesktopRecord *dr, int archive)
{
	GnomePilotConduitSyncAbs *conduit;
	GnomePilotDesktopRecord *gdr;
	gint retval = 0;

	conduit = ((gp_closure *)sh->data)->conduit;
	gdr = (GnomePilotDesktopRecord *)dr;
	sync_abs_fill_gdr (gdr);

	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_sync_abs_signals [ARCHIVE_RECORD],
			 0,
			 gdr, archive ? TRUE : FALSE, &retval);

	sync_abs_fill_dr (gdr);

	return retval;
}

static gint
gnome_pilot_conduit_sync_abs_match (SyncHandler *sh, PilotRecord *pr, DesktopRecord **dr)
{
	GnomePilotConduitSyncAbs *conduit;
	GnomePilotRecord *gpr;
	GnomePilotDesktopRecord *gdr = NULL;
	gint retval = 0;

	conduit = ((gp_closure *)sh->data)->conduit;
	gpr = sync_abs_pr_to_gpr (pr);

	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_sync_abs_signals [MATCH],
			 0,
			 gpr, &gdr, &retval);

	if (gdr != NULL)
		sync_abs_fill_dr (gdr);

	*dr = (DesktopRecord *)gdr;
	
	g_free (gpr);
	
	return retval;
}

static gint
gnome_pilot_conduit_sync_abs_free_match (SyncHandler *sh, DesktopRecord *dr)
{
	GnomePilotConduitSyncAbs *conduit;
	GnomePilotDesktopRecord *gdr;
	gint retval = 0;

	conduit = ((gp_closure *)sh->data)->conduit;
	gdr = (GnomePilotDesktopRecord *)dr;
	sync_abs_fill_gdr (gdr);

	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_sync_abs_signals [FREE_MATCH],
			 0,
			 gdr, &retval);

	return retval;
}

static gint
gnome_pilot_conduit_sync_abs_prepare (SyncHandler *sh, DesktopRecord *dr, PilotRecord *pr)
{
	GnomePilotConduitSyncAbs *conduit;
	GnomePilotDesktopRecord *gdr;
	GnomePilotRecord *gpr = NULL;
	gint retval = 0;

	conduit = ((gp_closure *)sh->data)->conduit;
	gdr = (GnomePilotDesktopRecord *)dr;
	sync_abs_fill_gdr (gdr);
	gpr = sync_abs_pr_to_gpr (pr);

	g_signal_emit   (G_OBJECT (conduit),
			 pilot_conduit_sync_abs_signals [PREPARE],
			 0,
			 gdr, gpr, &retval);

	*pr = sync_abs_gpr_to_pr (gpr);
	g_free (gpr);
	
	return retval;
}

/**
 * gnome_pilot_conduit_sync_abs_set_num_local_records:
 * @conduit: a sync abs conduit
 * @num: the total number of records on the desktop
 * 
 * This data set by this function is used to show progress to the user
 **/
void
gnome_pilot_conduit_sync_abs_set_num_local_records (GnomePilotConduitSyncAbs *conduit,
						    gint num) 
{
	g_return_if_fail (conduit != NULL);
	g_return_if_fail (GNOME_IS_PILOT_CONDUIT_SYNC_ABS (conduit));
	conduit->num_local_records = num;
}

/**
 * gnome_pilot_conduit_sync_abs_set_num_updated_local_records:
 * @conduit: a sync abs conduit
 * @num: the total number records on the desktop that were updated
 * 
 * This data set by this function is used to show progress to the user
 **/
void
gnome_pilot_conduit_sync_abs_set_num_updated_local_records (GnomePilotConduitSyncAbs *conduit,
							    gint num) 
{
	g_return_if_fail (conduit != NULL);
	g_return_if_fail (GNOME_IS_PILOT_CONDUIT_SYNC_ABS (conduit));
       	conduit->num_updated_local_records = num;
}

/**
 * gnome_pilot_conduit_sync_abs_set_num_new_local_records:
 * @conduit: a sync abs conduit
 * @num: the total number of new records on the desktop
 * 
 * This data set by this function is used to show progress to the user
 **/
void
gnome_pilot_conduit_sync_abs_set_num_new_local_records (GnomePilotConduitSyncAbs *conduit,
							gint num) 
{
	g_return_if_fail (conduit != NULL);
	g_return_if_fail (GNOME_IS_PILOT_CONDUIT_SYNC_ABS (conduit));
	conduit->num_new_local_records = num;
}

/**
 * gnome_pilot_conduit_sync_abs_set_num_deleted_local_records:
 * @conduit: a sync abs conduit
 * @num: the total number of deleted records on the desktop
 * 
 * This data set by this function is used to show progress to the user
 **/
void
gnome_pilot_conduit_sync_abs_set_num_deleted_local_records (GnomePilotConduitSyncAbs *conduit,
							    gint num) 
{
	g_return_if_fail (conduit != NULL);
	g_return_if_fail (GNOME_IS_PILOT_CONDUIT_SYNC_ABS (conduit));
	conduit->num_deleted_local_records = num;
}





