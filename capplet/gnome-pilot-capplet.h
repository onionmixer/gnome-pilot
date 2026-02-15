/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gnome-pilot-capplet.h
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

#ifndef _GNOME_PILOT_CAPPLET_H_
#define _GNOME_PILOT_CAPPLET_H_

#include <gnome-pilot-client.h>
#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define GNOME_PILOT_TYPE_CAPPLET			(gnome_pilot_capplet_get_type ())
#define GNOME_PILOT_CAPPLET(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GNOME_PILOT_TYPE_CAPPLET, GnomePilotCapplet))
#define GNOME_PILOT_CAPPLET_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GNOME_PILOT_TYPE_CAPPLET, GnomePilotCappletClass))
#define GNOME_PILOT_IS_CAPPLET(obj)			(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GNOME_PILOT_TYPE_CAPPLET))
#define GNOME_PILOT_IS_CAPPLET_CLASS(klass)		(G_TYPE_CHECK_CLASS_TYPE ((obj), GNOME_PILOT_TYPE_CAPPLET))


typedef struct _GnomePilotCapplet        GnomePilotCapplet;
typedef struct _GnomePilotCappletPrivate GnomePilotCappletPrivate;
typedef struct _GnomePilotCappletClass   GnomePilotCappletClass;

struct _GnomePilotCapplet {
	GtkDialog parent;

	GnomePilotCappletPrivate *priv;
};

struct _GnomePilotCappletClass {
	GtkDialogClass parent_class;
};


GType              gnome_pilot_capplet_get_type (void);
GnomePilotCapplet *gnome_pilot_capplet_new      (GnomePilotClient *gpc);

void gnome_pilot_capplet_update (GnomePilotCapplet *gpcap);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _GNOME_PILOT_CAPPLET_H_ */
