/* pilot.c
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

/* handles pilot issues */

#include <config.h>
#include <glib.h>
#include <pi-util.h>

#include <gnome-pilot-config.h>
#include "pilot.h"
#include "util.h"

/* create new pilotstate structure, initialize to sane state */
PilotState
*newPilotState (void)
{
    PilotState *p;
    p = g_new0(PilotState, 1);
    return p;
}

static void
copy_device (GPilotDevice *device, PilotState *dest)
{
    GPilotDevice *device2 = gpilot_device_new ();
    device2->name = g_strdup (device->name);
    device2->port = g_strdup (device->port);
    device2->speed = device->speed;
    device2->type = device->type;
    device2->timeout = device->timeout;
    dest->devices = g_list_append (dest->devices, device2);
}

static void
copy_pilot (GPilotPilot *pilot, PilotState *dest)
{
    GPilotPilot *pilot2 = gpilot_pilot_new ();
    pilot2->name = g_strdup (pilot->name);
    pilot2->passwd = g_strdup (pilot->passwd);
    pilot2->pilot_username = g_strdup (pilot->pilot_username);
    pilot2->pilot_id = pilot->pilot_id;
    pilot2->creation = pilot->creation;
    pilot2->romversion = pilot->romversion;
    pilot2->sync_options.basedir = g_strdup (pilot->sync_options.basedir);
    pilot2->pilot_charset = g_strdup (pilot->pilot_charset);
    dest->pilots = g_list_append (dest->pilots, pilot2);
}

void
copyPilotState (PilotState *dest, PilotState *src)
{
    dest->syncPCid = src->syncPCid;
    dest->progress_stepping = src->progress_stepping;
    if (src->pilots) g_list_foreach (src->pilots,(GFunc)copy_pilot, dest);
    if (src->devices) g_list_foreach (src->devices,(GFunc)copy_device, dest);
}

PilotState*
dupPilotState (PilotState *src)
{
    PilotState *p;
    p = g_new0(PilotState, 1);
    copyPilotState (p, src);
    return p;
}

void
freePilotState (PilotState *state)
{
    g_list_foreach (state->pilots,(GFunc)gpilot_pilot_free, NULL);
    g_list_free (state->pilots);
    g_list_foreach (state->devices,(GFunc)gpilot_device_free, NULL);
    g_list_free (state->devices);
    g_free (state);
}

static gint
loadHostID (PilotState *p)
{
    guint     id;
    gboolean  ret;
    GError   *error = NULL;
    GKeyFile *kfile = NULL;

    kfile = get_gpilotd_kfile ();
    if (kfile == NULL) {
        p->syncPCid = random ();
        return -1;
    }

    ret = 0;
    id = g_key_file_get_integer (kfile, "General", "sync_PC_Id", &error);
    if (error) {
        g_warning (_("Unable load key gpilotd/General/sync_PC_Id: %s"), error->message);
        g_error_free (error);
        error = NULL;
        ret = -1;
        p->syncPCid = random ();
    } else {
	if (id ==0)
		p->syncPCid = random ();
	else
		p->syncPCid = id;
    }

    p->progress_stepping = g_key_file_get_integer (kfile, "General", "progress_stepping", &error);
    if (error) {
        g_warning (_("Unable load key gpilotd/General/progress_stepping: %s"), error->message);
        g_error_free (error);
        error = NULL;
	p->progress_stepping = 5;
    }
  
    g_key_file_free (kfile);
    return ret;
}

static gint
loadDeviceCfg (PilotState *p)
{
  GPilotDevice *device;
  gchar        *iDevice;
  GKeyFile     *kfile = NULL;
  GError       *error = NULL;
  int i, num;

  kfile = get_gpilotd_kfile ();
  num = g_key_file_get_integer (kfile, "General", "num_devices", NULL);

  if (num == 0) {
      g_message ("No pilot cradle information located");
      p->devices = NULL;
      g_key_file_free (kfile);
      return -1;
  } else {
	  for (i=0;i<num;i++){
		  device = g_new0(GPilotDevice, 1);
                  iDevice = g_strdup_printf ("Device%d", i);
		  
		  device->type = g_key_file_get_integer (kfile, iDevice, "type", &error);
		  if (error) {
			  g_warning (_("Unable load key gpilotd/%s/type: %s"), iDevice, error->message);
			  g_error_free (error);
			  error = NULL;
			  device->type = 0;
		  };

		  if (device->type == PILOT_DEVICE_SERIAL) {
			  g_message ("Cradle Type -> Serial");
		  } else if (device->type == PILOT_DEVICE_USB_VISOR) {
			  g_message ("Cradle Type -> USB");
		  } else if (device->type == PILOT_DEVICE_IRDA) {
			  g_message ("Cradle Type -> IrDA");
		  } else if (device->type == PILOT_DEVICE_NETWORK) {
			  g_message ("Cradle Type -> Network");
		  } else if (device->type == PILOT_DEVICE_BLUETOOTH) {
			  g_message ("Cradle Type -> Bluetooth");
		  } else {
			  g_warning ("Unknown Cradle Type");
		  }

		  device->name = g_key_file_get_string (kfile, iDevice, "name", &error);
		  if (error) {
			  g_warning (_("Unable load key gpilotd/%s/name: iDevice, %s"), iDevice, error->message);
			  g_error_free (error);
			  error = NULL;
			  g_free (device->name);
			  device->name = g_strdup_printf ("Cradle%d", i);
		  };
		  g_message ("cradle device name -> %s", device->name);

		  if (device->type == PILOT_DEVICE_NETWORK) {
			  device->ip = g_key_file_get_string (kfile, iDevice, "ip", NULL);
			  g_message ("cradle network ip -> %s", device->ip);
		  } else if (device->type == PILOT_DEVICE_BLUETOOTH) {
			  /* no more parameters */
		  } else {
			  device->port = g_key_file_get_string (kfile, iDevice, "device", NULL);
			  g_message ("cradle device name -> %s", device->port);
			  device->speed = g_key_file_get_integer (kfile, iDevice, "speed", &error);
			  if (error) {
				  g_warning (_("Unable load key gpilotd/%s/speed: %s"), iDevice, error->message);
				  g_error_free (error);
				  error = NULL;
				  device->speed = 9600;
			  };
			  g_message ("Pilot Speed  -> %d", device->speed);  		  
		  }
		  device->timeout = g_key_file_get_integer (kfile, iDevice, "timeout", &error);
		  if (error) {
			  g_warning (_("Unable load key gpilotd/%s/timeout: %s"), iDevice, error->message);
			  g_error_free (error);
			  error = NULL;
			  device->timeout = 2;
		  };
		  g_message ("Timeout -> %d", device->timeout);
		  g_free (iDevice);
		  p->devices = g_list_append (p->devices, device);
	  }
  }

  g_key_file_free (kfile);
  return 0;
}


static gint
loadPilotPilot (PilotState *p)
{
  GPilotPilot *pilot;
  gchar *iPilot;
  int i, num;
  GKeyFile *kfile;
  GError *error = NULL;

  kfile = get_gpilotd_kfile ();

  num = g_key_file_get_integer (kfile, "General", "num_pilots", NULL);

  if (num == 0) {
      g_message ("No pilot userid/username information located");
      p->pilots = NULL;
      g_key_file_free (kfile);
      return -1;
  } else {
	  for (i=0;i<num;i++){
		  pilot = g_new0(GPilotPilot, 1);
		  iPilot = g_strdup_printf ("Pilot%d", i);

                  pilot->name = g_key_file_get_string (kfile, iPilot, "name", &error);
		  if (error) {
			  g_warning (_("Unable load key gpilotd/%s/name: %s"), iPilot, error->message);
			  g_error_free (error);
			  error = NULL;
			  g_free (pilot->name);
			  pilot->name = g_strdup ("MyPilot");
		  };
		  g_message ("Pilot name -> %s", pilot->name);

		  pilot->pilot_id = g_key_file_get_integer (kfile, iPilot, "pilotid", &error);
		  if (error) {
			  g_warning (_("Unable load key gpilotd/%s/pilotid: %s"), iPilot, error->message);
			  g_error_free (error);
			  error = NULL;
			  pilot->pilot_id = getuid ();
                  }
		  g_message ("Pilot id   -> %d", pilot->pilot_id);
		  /* username is stored as utf8 */
		  pilot->pilot_username = g_key_file_get_string (kfile, iPilot, "pilotusername", NULL);
		  g_message ("Pilot username -> %s", pilot->pilot_username);
	  
		  pilot->creation = g_key_file_get_integer (kfile, iPilot, "creation", NULL);
		  pilot->romversion = g_key_file_get_integer (kfile, iPilot, "romversion", NULL);
		  
		  g_message ("Pilot creation/rom = %lu/%lu", pilot->creation, pilot->romversion);

		  pilot->sync_options.basedir = g_key_file_get_string (kfile, iPilot, "basedir", NULL);
		  if (pilot->sync_options.basedir==NULL) {
			  pilot->sync_options.basedir = g_strdup_printf ("%s/%s", g_get_home_dir (), pilot->name);
		  }
	  
		  pilot->pilot_charset = g_key_file_get_string (kfile, iPilot, "charset", NULL);
		  if (pilot->pilot_charset == NULL)
			  pilot->pilot_charset =
			      g_strdup(get_default_pilot_charset());
		  pilot->pilot_charset = NULL;

		  pilot->number = i;
	  
		  g_free (iPilot);
		  
		  p->pilots = g_list_append (p->pilots, pilot);
	  }
  }

  g_key_file_free (kfile);
  return 0;
}

/* allocates a pilotstate, load pilot state, return 0 if ok, -1 otherwise*/
gint
loadPilotState (PilotState **pilotState)
{
    PilotState *p;

    p = newPilotState ();

    /* load host information */
    if (loadHostID (p) < 0) {
	g_message ("Unable to load host id information, assuming unset");
    }

    if (loadPilotPilot (p) < 0) {
	g_message ("Unable to load pilot id/username, assuming unset");	
    }

    if (loadDeviceCfg (p) < 0) {
	g_message ("Unable to load pilot cradle info, assuming unset");
    }

    *pilotState = p;
    return 0;
}


gint
savePilotState (PilotState *state)
{
  int i;
  GList *tmp;
  gchar *iDevice;
  gchar *iPilot;
  GKeyFile *kfile;

  kfile = get_gpilotd_kfile ();

  g_key_file_remove_group (kfile, "General", NULL);

  g_key_file_set_integer (kfile, "General", "sync_PC_Id", state->syncPCid);
  g_key_file_set_integer (kfile, "General", "progress_stepping", state->progress_stepping);

  tmp = state->devices;
  i=0;
  while (tmp!=NULL)
  {
	  GPilotDevice *device=(GPilotDevice*)tmp->data;
	  iDevice = g_strdup_printf ("Device%d", i);

	  g_key_file_remove_group (kfile, iDevice, NULL);
	  g_key_file_set_integer (kfile, iDevice, "type", device->type);
	  g_key_file_set_string (kfile, iDevice, "name", device->name);
	  if (device->type == PILOT_DEVICE_NETWORK) {
		  g_key_file_set_string (kfile, iDevice, "ip", device->ip);
	  } else if (device->type == PILOT_DEVICE_BLUETOOTH) {
		  /* no further info stored */
	  } else {
		  g_key_file_set_string (kfile, iDevice, "device", device->port);
		  g_key_file_set_integer (kfile, iDevice, "speed", device->speed);
	  }
	  g_key_file_set_integer (kfile, iDevice, "timeout", device->timeout);
	  g_free (iDevice);
	  tmp = tmp->next;
	  i++;
  }  
  if (i>0) {
      g_key_file_set_integer (kfile, "General", "num_devices", i);
  }

  tmp = state->pilots;
  i=0;
  while (tmp!=NULL)
  {
	  GPilotPilot *pilot=(GPilotPilot*)tmp->data;
	  iPilot = g_strdup_printf ("Pilot%d", i);

	  g_key_file_remove_group (kfile, iPilot, NULL);
	  g_key_file_set_string (kfile, iPilot, "name", pilot->name);
	  g_key_file_set_integer (kfile, iPilot, "pilotid", pilot->pilot_id);
	  g_key_file_set_integer (kfile, iPilot, "creation", pilot->creation);
	  g_key_file_set_integer (kfile, iPilot, "romversion", pilot->romversion);
	  /* store username as utf8 */
	  g_key_file_set_string (kfile, iPilot, "pilotusername", pilot->pilot_username);
	  g_key_file_set_string (kfile, iPilot, "basedir", pilot->sync_options.basedir);
	  g_key_file_set_string (kfile, iPilot, "charset", pilot->pilot_charset);
	  g_free (iPilot);
	  tmp = tmp->next;
	  i++;
  }
  if (i>0) {
      g_key_file_set_integer (kfile, "General", "num_pilots", i);
  }

  save_gpilotd_kfile (kfile);
  g_key_file_free (kfile);

  return 0;
}


GList *
load_conduit_list (GPilotPilot *pilot)
{
	GList *conduits = NULL, *conduit_states = NULL;
	gchar *buf;

	gnome_pilot_conduit_management_get_conduits (&conduits, GNOME_PILOT_CONDUIT_MGMT_NAME);
	conduits = g_list_sort (conduits, (GCompareFunc) strcmp);
	while (conduits != NULL) {
		ConduitState *conduit_state = g_new0 (ConduitState,1);

		conduit_state->name = g_strdup ((gchar*)conduits->data);
		conduit_state->pilot = pilot;
		conduit_state->management = gnome_pilot_conduit_management_new (conduit_state->name, GNOME_PILOT_CONDUIT_MGMT_NAME);
		conduit_state->config = gnome_pilot_conduit_config_new (conduit_state->management, pilot->pilot_id);
		gnome_pilot_conduit_config_load_config (conduit_state->config);

		conduit_state->description = g_strdup ((gchar*) gnome_pilot_conduit_management_get_attribute (conduit_state->management, "description", NULL));
		conduit_state->icon = g_strdup ((gchar*)gnome_pilot_conduit_management_get_attribute (conduit_state->management, "icon", NULL));
		if (conduit_state->icon == NULL || g_file_test (conduit_state->icon, G_FILE_TEST_IS_REGULAR)==FALSE) {
			conduit_state->icon = g_strdup_printf ("%s/%s", GNOMEPIXMAPSDIR, "gnome-palm-conduit.png");
		}
		
		buf = (gchar*) gnome_pilot_conduit_management_get_attribute (conduit_state->management, "settings", NULL);
		if (buf == NULL || buf[0] != 'T') 
			conduit_state->has_settings = FALSE;
		else 
			conduit_state->has_settings = TRUE;
			
		buf = (gchar*) gnome_pilot_conduit_management_get_attribute (conduit_state->management, "default-synctype", NULL);
		if (buf == NULL) 
			conduit_state->default_sync_type = GnomePilotConduitSyncTypeNotSet;
		else 
			conduit_state->default_sync_type = gnome_pilot_conduit_sync_type_str_to_int (buf);

		conduit_state->enabled = gnome_pilot_conduit_config_is_enabled (conduit_state->config, NULL);
		conduit_state->sync_type = conduit_state->config->sync_type;
		conduit_state->first_sync_type = conduit_state->config->first_sync_type;

		conduit_state->orig_enabled = conduit_state->enabled;
		if (conduit_state->enabled) 
			conduit_state->orig_sync_type = conduit_state->sync_type;
		else 
			conduit_state->orig_sync_type = GnomePilotConduitSyncTypeNotSet;
		conduit_state->orig_first_sync_type = conduit_state->first_sync_type;
		conduit_states = g_list_append (conduit_states, conduit_state);
		conduits = conduits->next;

		buf = (gchar*) gnome_pilot_conduit_management_get_attribute (conduit_state->management, "valid-synctypes", NULL);
		if (buf != NULL) {
			gchar **sync_types = g_strsplit (buf, " ", 0);
			int count = 0;

			while (sync_types[count]) {
				conduit_state->valid_synctypes = g_list_append (conduit_state->valid_synctypes, GINT_TO_POINTER (gnome_pilot_conduit_sync_type_str_to_int (sync_types[count])));
				count++;
			}
			g_strfreev (sync_types);
		}
	}

	return conduit_states;
}

void
free_conduit_list (GList *conduits)
{
	/* FIXME Properly free each state */

	g_list_free (conduits);
}
