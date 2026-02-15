/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- *//* 
 * Copyright (C) 1998-2000 Free Software Foundation
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
 *
 */

#ifndef __GPILOTD_QUEUE_IO__
#define __GPILOTD_QUEUE_IO__

#include <glib.h>
#include "gnome-pilot-conduit.h"
/* 
   new request types require additions to 
   load_requests, 
   store_request,
   request_type_from_string
   gpc_purge_requests
   gpc_queue_load_requests

   if system level request
   gpilotd.c:do_cradle_events
   is_system_related
   */

typedef enum {
  /* these are stored as [<pilotid>-<sequence>] */
  GREQ_PILOT_EVENT,
  GREQ_INSTALL,
  GREQ_RESTORE,
  GREQ_CONDUIT,

  /* these are stored as [</dev/bla>-<sequence>] */
  GREQ_CRADLE_EVENT,
  GREQ_GET_SYSINFO,
  GREQ_GET_USERINFO,
  GREQ_SET_USERINFO,
  GREQ_NEW_USERINFO,

  GREQ_INVALID=-1
} GPilotRequestType;

typedef struct GPilotRequest {
	GPilotRequestType type;  /* identifies the request type */
	guint32 pilot_id;        /* identifies the pilot */
	guint32 handle;          /* this is set by gpc_queue_store_request */
	guint timeout;           /* zero for no timeout */
	gchar *cradle;           /* NULL unless event is cradle specific (SET\GET userinfo) */
	gchar *client_id;        /* identifying id, eg. IOR */
	
	/* these are the parameters for the requests, read as you wish */
	union parameters {
		struct {
			gchar *filename;
			gchar *description;
		} install;
		struct {
			gchar *directory;
		} restore;
		struct {
			gchar *name;
			GnomePilotConduitSyncType how;
		} conduit;
		struct {
			gchar *password;
			gchar *user_id;
			gboolean continue_sync;
			guint32 pilot_id;
		} set_userinfo;
		struct {
			int dummy;
		} get_userinfo;
		struct {
			int dummy;
		} get_sysinfo;
	} parameters;
	
	/* this is data for the queue management, don't use! */
	struct {
		gchar *section_name;
	} queue_data;
} GPilotRequest;

/* load requests from stored files */
GList* gpc_queue_load_requests(guint32 pilot_id,
			       GPilotRequestType type,
			       gboolean all); 
GPilotRequest* gpc_queue_load_request(guint32 pilot_id,
				      gboolean system,
				      guint num);

GList* gpc_queue_load_requests_for_cradle(gchar *cradle);

/* store a request in .gnome/gnome-pilot.d/queues/PILOT_ID */
guint gpc_queue_store_request(GPilotRequest req);

/* Here's some specific stores... */
guint gpc_queue_store_set_userinfo_request (guint timeout, 
					    const gchar *cradle, 
					    const gchar *client_id,
					    const gchar *username,
					    guint userid,
					    gboolean continue_sync);

/* mark a request as processed, and purge from the queue files.
   This call also frees the data associated with the request.
 */
void gpc_queue_purge_request(GPilotRequest **req);
void gpc_queue_purge_request_point(guint32 pilot_id,guint num);

/* returns next time for request timeout (in secs) */


#endif

