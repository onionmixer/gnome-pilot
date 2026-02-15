/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-pilot-cdialog.h
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

#ifndef _GNOME_PILOT_CDIALOG_H_
#define _GNOME_PILOT_CDIALOG_H_

G_BEGIN_DECLS

#define GNOME_PILOT_TYPE_CDIALOG			(gnome_pilot_cdialog_get_type ())
#define GNOME_PILOT_CDIALOG(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GNOME_PILOT_TYPE_CDIALOG, GnomePilotCDialog))
#define GNOME_PILOT_CDIALOG_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GNOME_PILOT_TYPE_CDIALOG, GnomePilotCDialogClass))
#define GNOME_PILOT_IS_CDIALOG(obj)			(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GNOME_PILOT_TYPE_CDIALOG))
#define GNOME_PILOT_IS_CDIALOG_CLASS(klass)		(G_TYPE_CHECK_CLASS_TYPE ((obj), GNOME_PILOT_TYPE_CDIALOG))


typedef struct _GnomePilotCDialog        GnomePilotCDialog;
typedef struct _GnomePilotCDialogPrivate GnomePilotCDialogPrivate;
typedef struct _GnomePilotCDialogClass   GnomePilotCDialogClass;

struct _GnomePilotCDialog {
	GObject parent;

	GnomePilotCDialogPrivate *priv;
};

struct _GnomePilotCDialogClass {
	GObjectClass parent_class;
};


GType    gnome_pilot_cdialog_get_type (void);
GObject *gnome_pilot_cdialog_new      (ConduitState *state);

GnomePilotConduitSyncType gnome_pilot_cdialog_first_sync_type (GnomePilotCDialog *gpcd);
GnomePilotConduitSyncType gnome_pilot_cdialog_sync_type (GnomePilotCDialog *gpcd);

gboolean gnome_pilot_cdialog_run_and_close (GnomePilotCDialog *gpcd, GtkWindow *parent);

G_END_DECLS

#endif /* _GNOME_PILOT_CDIALOG_H_ */
