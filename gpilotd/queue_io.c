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

#include <glib/gi18n.h>
#include "queue_io.h"
#include "gnome-pilot-config.h"
#include "gpilot-daemon.h"

#define NUMREQ "number-of-requests"

/* defines for entries in the queue file */

#define ENT_TYPE "type"
#define ENT_CLIENT_ID "client_id"
#define ENT_FILENAME "filename"
#define ENT_DESCRIPTION "description"
#define ENT_DIRECTORY "directory"
#define ENT_DEVICE "device"
#define ENT_PILOT_ID "pilot_id"
#define ENT_USER_ID "user_id"
#define ENT_PASSWORD "password"
#define ENT_CONDUIT "conduit"
#define ENT_HOW "synctype"
#define ENT_TIMEOUT "timeout"
#define ENT_HANDLE "handle"
#define ENT_CONT_SYNC "continue_sync"

static gint is_system_related (GPilotRequestType type) {
	switch(type) {
	case GREQ_CRADLE_EVENT:
	case GREQ_SET_USERINFO:
	case GREQ_GET_USERINFO:
	case GREQ_GET_SYSINFO:
	case GREQ_NEW_USERINFO:
		return 1;
	default:
		return 0;
	}
}

/*
  FIXME:
  crapcrap, set_section skal tage **, man skal ikke malloc den... duhduh!
*/

static void
set_section (guint32 pilot_id, GPilotRequestType type, gchar **section)
{
	g_assert(section != NULL);

	if (*section!=NULL) {
		g_warning("set_section: *section!=NULL, potiential leak!");
	}

	if(!is_system_related (type)) {
		(*section) = g_strdup_printf ("%u", pilot_id);
	} else {
		(*section) = g_strdup ("system");
	}
}

static guint32
set_section_num (guint32 pilot_id,
		 GPilotRequestType type,
		 gchar **section,
		 gint num)
{
	g_assert (section!=NULL);

	if (*section!=NULL) {
		g_warning("set_section_num: *section!=NULL, potiential leak!");
	}

	if (!is_system_related (type)) {
		(*section) = g_strdup_printf ("%u-%u", pilot_id, num);
		return pilot_id*65535+num;
	} else {
		(*section) = g_strdup_printf ("system-%d", num);
		return num;
	}
}

static GPilotRequestType request_type_from_string(gchar *str) {
	if (str==NULL) {
		g_warning (_("Error in queue, non-existing entry"));
		return GREQ_INVALID;
	}

	if(g_ascii_strcasecmp (str,"GREQ_INSTALL") == 0) return GREQ_INSTALL;
	if(g_ascii_strcasecmp (str,"GREQ_RESTORE") == 0) return GREQ_RESTORE;
	if(g_ascii_strcasecmp (str,"GREQ_CONDUIT") == 0) return GREQ_CONDUIT;
	if(g_ascii_strcasecmp (str,"GREQ_SET_USERINFO") == 0) return GREQ_SET_USERINFO;
	if(g_ascii_strcasecmp (str,"GREQ_GET_USERINFO") == 0) return GREQ_GET_USERINFO;
	if(g_ascii_strcasecmp (str,"GREQ_GET_SYSINFO") == 0) return GREQ_GET_SYSINFO;
	if(g_ascii_strcasecmp (str,"GREQ_NEW_USERINFO") == 0) return GREQ_NEW_USERINFO;

	return GREQ_INVALID;
}

GList* gpc_queue_load_requests_for_cradle(gchar *cradle) {
	GList *retval,*tmp,*it;

	g_assert(cradle!=NULL);

	retval = NULL;
	tmp = NULL;
	it = NULL;
	/* call with a system type request and all=TRUE, to get all the queued system requests */
	tmp = gpc_queue_load_requests (0, GREQ_CRADLE_EVENT, TRUE);

	it = g_list_first(tmp);
	while(it) {
		GPilotRequest *req;
		req = NULL;;

		req = (GPilotRequest*)it->data;
		if(req && (g_ascii_strcasecmp (req->cradle,cradle) == 0)) {
			retval = g_list_append(retval,req);
		}
		it = g_list_next(it);
	}

	g_list_free(tmp);

	return retval;
}

GList*
gpc_queue_load_requests (guint32 pilot_id, GPilotRequestType type, gboolean all)
{
	int num;
	GList *retval = NULL;
	gchar *section = NULL;
	GKeyFile *kfile;

	kfile = get_queue_kfile ();

	set_section (pilot_id, type, &section);
	num = g_key_file_get_integer (kfile, section, NUMREQ, NULL);
	g_free (section);

	for (;num>0;num--) {
		GPilotRequest *req;
		if (is_system_related (type)) {
			req = gpc_queue_load_request (pilot_id, TRUE, num);
		} else {
			req = gpc_queue_load_request (pilot_id, FALSE, num);
		}

		if(req==NULL) {
			continue;
		}
		if (req->type!=type && all==FALSE) {
			g_free (req);
			continue;
		}

		retval = g_list_append (retval, req);
	}

	g_key_file_free (kfile);
	return retval;
}

GPilotRequest* 
gpc_queue_load_request (guint32 pilot_id, gboolean _type, guint num)
{
	GPilotRequest *req;
	gchar *section = NULL;
	GPilotRequestType type;
	GKeyFile *kfile;

	if (_type==TRUE) {
		type = GREQ_CRADLE_EVENT; 
	} else {
		type = GREQ_PILOT_EVENT;
	}

	kfile = get_queue_kfile ();
	set_section_num (pilot_id, type, &section, num);

	req = g_new0 (GPilotRequest, sizeof (GPilotRequest));
	req->type = request_type_from_string (g_key_file_get_string (kfile, section, ENT_TYPE, NULL));
	if (req->type == GREQ_INVALID) {
		g_free (req);
		g_key_file_free (kfile);
		return NULL;
	}
  
	/* unless I store the sectionname _without_ trailing /, clean_section
	   can't delete it ? */
	g_free (section);
	section = NULL;
	req->handle = set_section_num (pilot_id, type, &section, num);
	req->queue_data.section_name = g_strdup (section);
	req->pilot_id = pilot_id;
  
	switch (req->type) {
	case GREQ_INSTALL:
		req->parameters.install.filename = g_key_file_get_string (kfile, section, ENT_FILENAME, NULL);
		req->parameters.install.description = g_key_file_get_string (kfile, section, ENT_DESCRIPTION, NULL);
		break;
	case GREQ_RESTORE:
		req->parameters.restore.directory = g_key_file_get_string (kfile, section, ENT_DIRECTORY, NULL);
		break;
	case GREQ_CONDUIT: {
		gchar *tmp;
		req->parameters.conduit.name = g_key_file_get_string (kfile, section, ENT_CONDUIT, NULL);
		tmp = g_key_file_get_string (kfile, section, ENT_HOW, NULL);
		req->parameters.conduit.how = gnome_pilot_conduit_sync_type_str_to_int (tmp);
		g_free(tmp);
	}
	break;
	case GREQ_GET_USERINFO:
		break;
	case GREQ_GET_SYSINFO:
		break;
	case GREQ_NEW_USERINFO: /* shares parameters with SET_USERINFO */
		req->parameters.set_userinfo.password = g_key_file_get_string (kfile, section, ENT_PASSWORD, NULL);
		req->parameters.set_userinfo.user_id = g_key_file_get_string (kfile, section, ENT_USER_ID, NULL);
		req->parameters.set_userinfo.pilot_id = g_key_file_get_integer (kfile, section, ENT_PILOT_ID, NULL);
		break;
	case GREQ_SET_USERINFO:
		req->parameters.set_userinfo.password = g_key_file_get_string (kfile, section, ENT_PASSWORD, NULL);
		req->parameters.set_userinfo.user_id = g_key_file_get_string (kfile, section, ENT_USER_ID, NULL);
		req->parameters.set_userinfo.pilot_id = g_key_file_get_integer (kfile, section, ENT_PILOT_ID, NULL);
		req->parameters.set_userinfo.continue_sync = g_key_file_get_boolean (kfile, section, ENT_CONT_SYNC, NULL);
		break;
	default: 
		g_assert_not_reached();
		break;
	}
	req->cradle = g_key_file_get_string (kfile, section, ENT_DEVICE, NULL);
	req->client_id = g_key_file_get_string (kfile, section, ENT_CLIENT_ID, NULL);
	req->timeout = g_key_file_get_integer (kfile, section, ENT_TIMEOUT, NULL);
	req->handle = g_key_file_get_integer (kfile, section, ENT_HANDLE, NULL);

	g_free (section);
	g_key_file_free (kfile);
	return req;
}

/*
  FIXME: Leaks! gpc_queue_store_requests is called from
  gpilot_daemon in a return call, with strings that are strdupped,
  thus they're leaked.
 */

guint
gpc_queue_store_request (GPilotRequest req)
{
	guint num;
	guint32 handle_num;
	gchar *section = NULL;
	GKeyFile *kfile;

	set_section (req.pilot_id, req.type, &section);

	kfile = get_queue_kfile ();

	num = g_key_file_get_integer (kfile, section, NUMREQ, NULL);
	num++;
	g_key_file_set_integer (kfile, section, NUMREQ, num);
	g_free (section);

	section = NULL;
	handle_num = set_section_num (req.pilot_id, req.type, &section, num);
  
	switch (req.type) {
	case GREQ_INSTALL: 
		g_key_file_set_string (kfile, section, ENT_TYPE, "GREQ_INSTALL"); 
		g_key_file_set_string (kfile, section, ENT_FILENAME, req.parameters.install.filename);
		g_key_file_set_string (kfile, section, ENT_DESCRIPTION, req.parameters.install.description);
		break;
	case GREQ_RESTORE: 
		g_key_file_set_string (kfile, section, ENT_TYPE, "GREQ_RESTORE"); 
		g_key_file_set_string (kfile, section, ENT_DIRECTORY, req.parameters.restore.directory);
		break;
	case GREQ_CONDUIT: 
		g_message ("req.parameters.conduit.name = %s", req.parameters.conduit.name);
		g_key_file_set_string (kfile, section, ENT_TYPE, "GREQ_CONDUIT"); 
		g_key_file_set_string (kfile, section, ENT_CONDUIT, req.parameters.conduit.name);
		g_key_file_set_string (kfile, section, ENT_HOW,
				       gnome_pilot_conduit_sync_type_int_to_str (req.parameters.conduit.how));
		break;
	case GREQ_NEW_USERINFO: 
		g_key_file_set_string (kfile, section, ENT_TYPE, "GREQ_NEW_USERINFO"); 
		g_key_file_set_string (kfile, section, ENT_DEVICE, req.cradle);
		g_key_file_set_string (kfile, section, ENT_USER_ID, req.parameters.set_userinfo.user_id);
		g_key_file_set_integer (kfile, section, ENT_PILOT_ID, req.parameters.set_userinfo.pilot_id);
		break;
	case GREQ_SET_USERINFO: 
		g_key_file_set_string (kfile, section, ENT_TYPE, "GREQ_SET_USERINFO"); 
		g_key_file_set_string (kfile, section, ENT_DEVICE, req.cradle);
		g_key_file_set_string (kfile, section, ENT_PASSWORD, req.parameters.set_userinfo.password);
		g_key_file_set_string (kfile, section, ENT_USER_ID, req.parameters.set_userinfo.user_id);
		g_key_file_set_integer (kfile, section, ENT_PILOT_ID, req.parameters.set_userinfo.pilot_id);
		g_key_file_set_boolean (kfile, section, ENT_CONT_SYNC, req.parameters.set_userinfo.continue_sync);
		break;
	case GREQ_GET_USERINFO: 
		g_key_file_set_string (kfile, section, ENT_TYPE, "GREQ_GET_USERINFO"); 
		g_key_file_set_string (kfile, section, ENT_DEVICE,req.cradle);
		break;
	case GREQ_GET_SYSINFO: 
		g_key_file_set_string (kfile, section, ENT_TYPE, "GREQ_GET_SYSINFO"); 
		g_key_file_set_string (kfile, section, ENT_DEVICE, req.cradle);
		break;
	default: 
		g_assert_not_reached ();
		break;
	}
	g_key_file_set_integer (kfile, section, ENT_TIMEOUT, req.timeout);
	g_key_file_set_integer (kfile, section, ENT_HANDLE, handle_num);
	g_key_file_set_string (kfile, section, ENT_CLIENT_ID, req.client_id);
    
	g_free (section);
	save_queue_kfile (kfile);
	g_key_file_free (kfile);

	LOG (("assigned handle num %u",handle_num));
	return handle_num;
}

guint gpc_queue_store_set_userinfo_request (guint timeout, 
					    const gchar *cradle, 
					    const gchar *client_id,
					    const gchar *username,
					    guint userid,
					    gboolean continue_sync)
{
	GPilotRequest req;

	req.type = GREQ_SET_USERINFO;
	req.pilot_id = 0;
	req.timeout = timeout;
	req.cradle = g_strdup(cradle);
	req.client_id = g_strdup(client_id);
	req.parameters.set_userinfo.password = NULL;
	req.parameters.set_userinfo.user_id = g_strdup(username);
	req.parameters.set_userinfo.pilot_id = userid;
	req.parameters.set_userinfo.continue_sync = continue_sync;

	return gpc_queue_store_request(req);	
}


/* FIXME:This is nice, but if there's 10 requests, and you delete 2-9,
leaving two behind, they're enumerated wrong. Eith renumerate them, or
do the fix in load of requests. Or will that scenario never appear ? Yes it will,
if gpilotd trashs before all requests are done, the last ones will be left in the
file, that can be fixed by handling them in reverse order... yech 

But since the handle is done using the enumeration (id << 16 + enum),
renummerating them would trash that.

*/

void 
gpc_queue_purge_request(GPilotRequest **req) 
{
	gchar *section = NULL;
	int num;
	GKeyFile *kfile;

	LOG (("gpc_queue_purge_request()"));

	g_return_if_fail (req != NULL);
	g_return_if_fail (*req != NULL);

	kfile = get_queue_kfile ();
	set_section ((*req)->pilot_id, (*req)->type, &section);
	num = g_key_file_get_integer (kfile, section, NUMREQ, NULL);
	num--;
	g_key_file_set_integer (kfile, section, NUMREQ, num);

	g_key_file_remove_group (kfile, (*req)->queue_data.section_name, NULL);

	switch((*req)->type) {
	case GREQ_INSTALL:
		unlink((*req)->parameters.install.filename);
		g_free((*req)->parameters.install.filename);
		g_free((*req)->parameters.install.description);
		break;
	case GREQ_RESTORE:
		g_free((*req)->parameters.restore.directory);
		break;
	case GREQ_CONDUIT:
		g_free((*req)->parameters.conduit.name);
		break;
	case GREQ_GET_USERINFO: 
		break;
	case GREQ_GET_SYSINFO: 
		break;
	case GREQ_NEW_USERINFO: 
	case GREQ_SET_USERINFO: 
		g_free((*req)->parameters.set_userinfo.user_id);
		g_free((*req)->parameters.set_userinfo.password);
		break;
	default: 
		g_assert_not_reached();
		break;
	}
	g_free((*req)->cradle);
	g_free((*req)->client_id);
	g_free((*req)->queue_data.section_name);
	g_free(*req);
	*req = NULL;
  
	g_free (section);

	save_queue_kfile (kfile);
	g_key_file_free (kfile);
} 

void 
gpc_queue_purge_request_point (guint32 pilot_id,
			       guint num) 
{
	GPilotRequest *req;

	if(pilot_id==0) {
                 /* no id, system level request, use system level request
		    type for load */
		g_message ("FISK: OST");
		req = gpc_queue_load_request (pilot_id, TRUE, num);
	} else {
                /* otherwise use a common as type */
		g_message ("FISK: KRYDDERSILD");
		req = gpc_queue_load_request (pilot_id, FALSE, num);
	}

	if(req) {
		gpc_queue_purge_request (&req);  
	} else {
		g_warning (_("fault: no request found for PDA %d, request %d"), pilot_id, num);
	}
}
