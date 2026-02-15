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

#ifndef _GNOME_PILOT_ASSISTANT_H_
#define _GNOME_PILOT_ASSISTANT_H_

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gnome-pilot-client.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define GNOME_PILOT_TYPE_ASSISTANT			(gnome_pilot_assistant_get_type ())
#define GNOME_PILOT_ASSISTANT(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GNOME_PILOT_TYPE_ASSISTANT, GnomePilotAssistant))
#define GNOME_PILOT_ASSISTANT_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GNOME_PILOT_TYPE_ASSISTANT, GnomePilotAssistantClass))
#define GNOME_PILOT_IS_ASSISTANT(obj)			(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GNOME_PILOT_TYPE_ASSISTANT))
#define GNOME_PILOT_IS_ASSISTANT_CLASS(klass)		(G_TYPE_CHECK_CLASS_TYPE ((obj), GNOME_PILOT_TYPE_ASSISTANT))


typedef struct _GnomePilotAssistant        GnomePilotAssistant;
typedef struct _GnomePilotAssistantPrivate GnomePilotAssistantPrivate;
typedef struct _GnomePilotAssistantClass   GnomePilotAssistantClass;

struct _GnomePilotAssistant {
	GObject parent;

	GnomePilotAssistantPrivate *priv;
};

struct _GnomePilotAssistantClass {
	GObjectClass parent_class;
};


GType    gnome_pilot_assistant_get_type (void);
GObject *gnome_pilot_assistant_new      (GnomePilotClient *gpc);

gboolean gnome_pilot_assistant_run_and_close (GnomePilotAssistant *gpd);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _GNOME_PILOT_ASSISTANT_H_ */
