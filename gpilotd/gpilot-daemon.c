/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- *//* 
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
 * Authors: Halton Huo <halton.huo@gmail.com>
 *
 */

#include "config.h"

/* for crypt () */
#ifdef USE_XOPEN_SOURCE
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 
#endif /* _XOPEN_SOURCE  */
#endif

#define _BSD_SOURCE 1		/* Or gethostname won't be declared properly
				   on Linux and GNU platforms. */

#include <unistd.h>
#include <pwd.h>
#include <sys/stat.h>
#include <regex.h>
#include <crypt.h>
#include <libxml/xmlmemory.h>
#include <libxml/entities.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <pi-dlp.h>
#include <pi-util.h>

#include "gpilot-daemon.h"
#include "gpilot-daemon-generated.h"
#include "gnome-pilot-structures.h"
#include "gnome-pilot-conduit-standard.h"
#include "queue_io.h"
#include "gnome-pilot-config.h"
#include "gpmarshal.h"
#include "gpilot-gui.h"
#include "manager.h"

#include <gio/gio.h>

#define GP_DBUS_NAME         "org.gnome.GnomePilot"
#define GP_DBUS_PATH         "/org/gnome/GnomePilot/Daemon"
#define GP_DBUS_INTERFACE    "org.gnome.GnomePilot.Daemon"

#define IS_STR_SET(x) (x != NULL && x[0] != '\0')

#define GPILOT_DAEMON_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPILOT_TYPE_DAEMON, GpilotDaemonPrivate))

#define DEBUG_CODE

static void     gpilot_daemon_class_init  (GpilotDaemonClass *klass);
static void     gpilot_daemon_init        (GpilotDaemon      *daemon);
static void     gpilot_daemon_finalize    (GObject           *object);

static gpointer daemon_object = NULL;

/* Set to true when the config should be reloaded */
static void monitor_channel (GPilotDevice *dev, GPilotContext *context);
static gboolean device_in (GIOChannel *, GIOCondition, GPilotContext *);
static gboolean device_err (GIOChannel *, GIOCondition, GPilotContext *);
static gboolean network_device_in (GIOChannel *, GIOCondition, GPilotContext *);
static gboolean network_device_err (GIOChannel *, GIOCondition, GPilotContext *);
static int pilot_connect (GPilotDevice *device,int *error);
static int check_usb_config (GPilotDevice *device);
static gboolean do_cradle_events (int pfd, GPilotContext *context, struct PilotUser *pu, GPilotDevice *device);
static void gpilot_syncing_unknown_pilot (struct PilotUser pu, int pfd, GPilotDevice *device, GPilotContext *context);
static void gpilot_syncing_known_pilot (GPilotPilot *pilot, struct PilotUser pu, int pfd, GPilotDevice *device, GPilotContext *context);
static void pilot_disconnect (int sd);
static void load_devices_xml (void);
static int  known_usb_device (gchar *match_str);
static gboolean sync_device (GPilotDevice *device, GPilotContext *context);

/* move these into GpilotDaemonPrivate... */
#include <gudev/gudev.h>
static gboolean gpilotd_gudev_init (void);
GUdevClient *gudev_client = NULL;

static guint visor_timeout_id = -1;
static guint udev_initialised = 0;
static GPtrArray *vendor_product_ids = NULL;
static GArray *product_net = NULL;

G_DEFINE_TYPE (GpilotDaemon, gpilot_daemon, G_TYPE_OBJECT)

struct GpilotDaemonPrivate
{
        GPilotContext   *gpilotd_context;
        GDBusConnection *connection;
};

/* Global D-Bus connection for signal emission from static functions */
static GDBusConnection *gdbus_connection = NULL;

enum {
        CONNECTED,
        DISCONNECTED,
        REQUEST_COMPLETED,
        USERINFO_REQUESTED,
        SYSINFO_REQUESTED,
        CONDUIT_START,
        CONDUIT_PROGRESS,
        CONDUIT_END,
        OVERALL_PROGRESS,
        DAEMON_MESSAGE,
        DAEMON_ERROR,
        CONDUIT_MESSAGE,
        CONDUIT_ERROR,
        PAUSED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void
gpilot_daemon_pause_device (GPilotDevice *device,
                            gpointer      data)
{
        if (device->io) {
                g_source_remove(device->in_handle);
                g_source_remove(device->err_handle);
        }
}

static gint
match_pilot_and_name(const GPilotPilot *pilot,
                     const gchar       *name)
{
        if(pilot) {
                return g_ascii_strcasecmp (name, pilot->name);
        }
        return -1; 
}

static gint
match_pilot_userID (const GPilotPilot   *p,
                    const guint32       *id)
{
        if(p->pilot_id == *id)
                return 0;
        return -1;
}

/* function to handle a usb device/vendor ID connection.
 * used by sysfs, hal, gdbus paths
 */
static void usb_device_added (int vendor_id, int product_id) {
	gboolean visor_net = FALSE;
	char *match_str;
	int i;
	GPilotDevice *device;
	GList *dev;
        GpilotDaemonPrivate *priv;

        priv = GPILOT_DAEMON (daemon_object)->priv;

	if (priv->gpilotd_context->paused) 
		return;

	load_devices_xml ();

	/* now look for a vendor/product match */
	match_str = g_strdup_printf ("Vendor=%04x ProdID=%04x",
	    vendor_id, product_id);
	i = known_usb_device(match_str);
	g_warning("found match: index=%d looking for: %s\n", i, match_str);
	g_free(match_str);
	if(i == -1)
		return;

	visor_net = g_array_index (product_net, gboolean, i);
	dev = priv->gpilotd_context->devices;
	while (dev != NULL) {
		device = dev->data;
		if (device->type == PILOT_DEVICE_USB_VISOR) {
			if (!visor_net)
				device->type = PILOT_DEVICE_SERIAL;
			/* problems have been reported with devices
 			 * not being ready for sync immediately,
 			 * so we wait for 0.4 seconds.  See
 			 * bugzilla.gnome #362565
 			 */
			usleep(400000);
			/* just try to sync.  Until I can talk to 
			 * the kernel guys this is the best way to 
			 * go. */
			sync_device (device, priv->gpilotd_context);

			if (!visor_net)
				device->type = PILOT_DEVICE_USB_VISOR;
			
			break;
		}
		
		dev = dev->next;
	}
}


static void gpilotd_gudev_handler  (GUdevClient *client, gchar *action, GUdevDevice *device,
    gpointer user_data)
{
	int vendor_id = -1, product_id = -1;
        GpilotDaemonPrivate *priv;
	const gchar *s = NULL;

	g_warning ("got usb device: %s", g_udev_device_get_sysfs_path(device));

        priv = GPILOT_DAEMON (daemon_object)->priv;

	if (priv->gpilotd_context->paused) 
		return;
	if (g_strncasecmp (action, "add", 3) == 0) {
		s = g_udev_device_get_property (device, "ID_VENDOR_ID");
		if (s != NULL)
			vendor_id = g_ascii_strtoll (s, NULL, 16);
		s = g_udev_device_get_property (device, "ID_MODEL_ID");
		if (s != NULL)
			product_id = g_ascii_strtoll (s, NULL, 16);
		usb_device_added(vendor_id, product_id);
	}
}

static gboolean
gpilotd_gudev_init (void)
{
	const gchar * const udev_subsystems[2] = { "usb/usb_device", NULL};

	/* Just use g_udev_client / g_udev_device
	   http://www.kernel.org/pub/linux/utils/kernel/hotplug/gudev/GUdevClient.html
	*/

	if (gudev_client != NULL)
		return TRUE;

	gudev_client = g_udev_client_new (udev_subsystems);

        g_signal_connect (G_OBJECT (gudev_client),"uevent", G_CALLBACK (gpilotd_gudev_handler), NULL);
	
	return TRUE;
}


static int
device_equal_by_io (GPilotDevice *dev, GIOChannel *io) 
{
	return !(dev->io == io);
}

static void
remove_device (GPilotContext *context, GPilotDevice *device) 
{
	GList *l;

	g_message ("Removing %s", device->name);

	l = g_list_find (context->devices, device);
	if (l != NULL) {
		gpilot_device_free (l->data);
		context->devices = g_list_remove (context->devices, l);
		g_list_free_1 (l);
	} else {
		g_message ("%s not found",device->name);
	}
}

static void 
pilot_set_baud_rate (GPilotDevice *device) 
{
	static char rate_buf[128];
	
	g_snprintf (rate_buf, 128, "PILOTRATE=%d", device->speed);
	g_message ("setting %s", rate_buf);
	putenv (rate_buf);
}

/*
  Sets *error to 1 on fatal error on the device, 2 on other errors , 0 otherwise.
 */
static int 
pilot_connect (GPilotDevice *device,int *error) 
{
#define MAX_TIME_FOR_PI_BIND 1000000 /* 1 second, or 1,000,000 usec. */
#define SLEEP_TIME_FOR_PI_BIND 50000 /* 50 ms */
	
	int sd, listen_sd, pf;
	int ret;

	int time_elapsed_pi_bind = 0;
	
	if (device->type != PILOT_DEVICE_NETWORK &&
	    device->type != PILOT_DEVICE_BLUETOOTH) {
		pilot_set_baud_rate (device);
	}
	
	switch (device->type) {
	case PILOT_DEVICE_SERIAL:
		pf = PI_PF_PADP;
		break;
	case PILOT_DEVICE_USB_VISOR:
		pf = PI_PF_NET;
		break;
	case PILOT_DEVICE_IRDA:
		pf = PI_PF_PADP;
		break;
	case PILOT_DEVICE_NETWORK:
	case PILOT_DEVICE_BLUETOOTH:
		pf = PI_PF_NET;
		break;
	default:
		pf = PI_PF_DLP;
		break;
	}
	
	if (device->type == PILOT_DEVICE_NETWORK ||
	    device->type == PILOT_DEVICE_BLUETOOTH) {
		/* In the case of network pi_sockets, the socket is already
		 * listening at this point, so move on to accept */
		listen_sd = device->fd;
	} else {
		/* pl 0.12.0 wants to autodetect the protocol, so pass DLP */
		/* (at time of writing there's a buglet in pl where if you
		 * _do_ pass in the correct NET protocol then pl will flush
		 * pending net input, which might just lose your first
		 * packet.
		 */
		listen_sd = pi_socket (PI_AF_PILOT, PI_SOCK_STREAM, PI_PF_DLP);
		if (listen_sd < 0) {
			g_warning ("pi_socket returned error %d (%s)",
			    listen_sd,
			    strerror (errno));
			if (error) *error = 1;
			return -1;
		}

		
		/* 
		 * When using DBUS and USB, the ttyUSB[01] device files are
		 * not created until after dbus has notified gpilotd that
		 * a file exists. This causes pi_bind to fail.
		 * To prevent a failure (and therefore sync), retry pi_bind
		 * for up to the time allotted, sleeping a little in-between
		 * tries.
		 */
		
		time_elapsed_pi_bind = 0;
		do {
			ret = pi_bind (listen_sd, device->port);
			if(ret < 0) {
				usleep(SLEEP_TIME_FOR_PI_BIND);
				time_elapsed_pi_bind += SLEEP_TIME_FOR_PI_BIND;
			}
		} while(ret<0 && time_elapsed_pi_bind < MAX_TIME_FOR_PI_BIND);
		
		if (ret < 0) {
			g_warning (_("Unable to bind to PDA"));
			if (error)
				*error = 1;
			pi_close(listen_sd);
			check_usb_config(device);
			return 0;
		}

		ret = pi_listen (listen_sd, 1);
		if (ret != 0) {
			g_warning ("pi_listen: %s", strerror (errno));
			if (error)
				*error = 2;
			pi_close(listen_sd);
			return 0;
		}
	}

	sd = pi_accept_to (listen_sd, NULL, NULL, device->timeout); 
	if (sd < 0) {
		g_warning ("pi_accept_to returned %d: %s", sd, strerror (errno));
		g_warning ("pi_accept_to: timeout was %d secs", device->timeout);
		if (error)
			*error = 2;
		if (device->type != PILOT_DEVICE_NETWORK &&
		    device->type != PILOT_DEVICE_BLUETOOTH) {
			pi_close(listen_sd);
		}
		return 0;
	}

	if (error)
		*error = 0;

	return sd;
}

static void
pilot_disconnect (int sd)
{
	int ret; 

	dlp_EndOfSync (sd, 0);
	ret = pi_close (sd);
	if (ret < 0) {
		g_warning ("error %d from pi_close.", ret);
	}

}

static void
write_sync_stamp (GPilotPilot *pilot, int pfd, struct PilotUser *pu,
		  guint32 last_sync_pc, time_t t)
{
	GKeyFile *kfile;
	gchar    *iPilot;
	
	pu->lastSyncPC = last_sync_pc;
	pu->lastSyncDate = t;
	
	kfile = get_gpilotd_kfile ();
	iPilot = g_strdup_printf ("Pilot%d", pilot->number);
	g_key_file_set_integer (kfile, iPilot, "sync_date", t);
	
	save_gpilotd_kfile (kfile);
	g_key_file_free (kfile);
	g_free (iPilot);

	dlp_WriteUserInfo (pfd, pu);
}

/** pilot lookup methods **/

static gint
match_pilot_and_majick (const GPilotPilot *pilot,
			unsigned long creation,
			unsigned long romversion)
{
	if (pilot->creation == creation &&
	    pilot->romversion == romversion) {
		g_message ("pilot %s %ld %ld matches %ld %ld", 
			   pilot->name, 
			   pilot->creation,
			   pilot->romversion, 
			   creation, 
			   romversion);
		return 1;
	}
	g_message ("pilot %s %ld %ld doesn't match %ld %ld", 
		   pilot->name, 
		   pilot->creation,
		   pilot->romversion, 
		   creation, 
		   romversion);
	return 0;
}


/* Checks whether visor module is loaded but 'usb:' device is selected,
 * or vice versa.  Returns 0 if the config looks okay, or -1 otherwise.
 * raises a gpilot_gui_warning_dialog and a g_warning if the config
 * looks incorrect.
 */
static int
check_usb_config (GPilotDevice *device)
{
#define MAXLINELEN 256
	char line[MAXLINELEN];
	gboolean visor_loaded;
	gboolean libusb_device;
	FILE *f;
	int error = 0;

	if (device->type != PILOT_DEVICE_USB_VISOR)
		return 0;
	visor_loaded = FALSE;
	libusb_device = TRUE; /* defaults, in case we skip block */
	f = fopen ("/proc/modules", "r");
	if (f) {
		while(fgets(line, MAXLINELEN, f) != NULL) {
			if (strncmp(line, "visor", 5) == 0) {
				visor_loaded = TRUE;
				break;
			}
		}
		fclose(f);
		libusb_device = (strncmp(device->port,
					 "usb:", 4) == 0);
	}
	if (libusb_device && visor_loaded) {
		g_snprintf(line, MAXLINELEN,_("Failed to connect using "
					      "device `%s', on port `%s'.  "
					      "Check your configuration, "
					      "as you requested "
					      "new-style libusb `usb:' "
					      "syncing, but have the "
					      "old-style `visor' kernel "
					      "module loaded.  "
					      "You may need to select a "
					      "`ttyUSB...' device."),
			   device->name, device->port);
		g_warning("%s", line);
		gpilot_gui_warning_dialog(line);
		error = 1;
	} else if (!libusb_device && !visor_loaded) {
		g_snprintf(line, MAXLINELEN,_("Failed to connect using "
					      "device `%s', on port `%s'.  "
					      "Check your configuration, "
					      "as you requested "
					      "old-style usbserial `ttyUSB' "
					      "syncing, but do not have the "
					      "usbserial `visor' kernel "
					      "module loaded.  "
					      "You may need to select a "
					      "`usb:' device."),
			   device->name, device->port);
		g_warning("%s", line);
		gpilot_gui_warning_dialog(line);
		error = 1;
	}
	return error;
}


/**************************/

/*
 * We get a pfd, a device and the context (called if a pilot
 * with id == 0 synchronizes)
 * Get whatever majick number we can get and try to id 
 * the pilot and offer to restore it. If it can id the
 * pilot, ask the user to choose one or "make a new pilot"
 */

static gboolean
gpilot_attempt_restore (struct PilotUser pu,
			int pfd, 
			GPilotDevice *device, 
			GPilotContext *context)
{
	struct SysInfo sysinfo;
	struct CardInfo cardinfo;
	GPilotPilot *pilot = NULL;
	GList *iterator;
	gboolean result = FALSE;
	
	dlp_ReadStorageInfo (pfd, 0, &cardinfo);
	dlp_ReadSysInfo (pfd, &sysinfo);

	if (context->pilots && context->pilots->next == NULL) {
		pilot = GPILOT_PILOT (g_list_nth (context->pilots, 0)->data);
		g_message ("D: Only one PDA (%s) profile...", pilot->name);
	} else {
		for (iterator = context->pilots; iterator; iterator = g_list_next (iterator)) {
			pilot = GPILOT_PILOT (iterator->data);
			if (match_pilot_and_majick (pilot, 
						    cardinfo.creation,
						    sysinfo.romVersion)) {
				break;
			}
		}
	}

	if (pilot) {
		GPilotPilot *a_pilot;
		a_pilot = gpilot_gui_restore (context, pilot);
		if (a_pilot) {
			dbus_notify_connected (pilot->name,pu);				
			result = gpilot_start_unknown_restore (pfd, device, a_pilot);
			dbus_notify_disconnected (pilot->name);
		}
	} else {
		/* MUST GO */
		gpilot_gui_warning_dialog ("no ident\n"
					   "restoring PDA with ident\n"
					   "c/r = %lu/%lu, exciting things\n"
					   "will soon be here...",
					   cardinfo.creation,
					   sysinfo.romVersion);
	}

	return TRUE;
}

/*
 * This function handles when sync_device (...) encounters an unknown pilot
 */

static void
gpilot_syncing_unknown_pilot (struct PilotUser pu, 
			      int pfd,
			      GPilotDevice *device, 
			      GPilotContext *context)
{

	g_warning (_("Unknown PDA, no userID/username match %ld"),pu.userID);
	/* FIXME: here, restoring one of the available pilots should be
	   offered to the user. Of course with password prompt if the user
	   has password set
	   bug # 8217 */
	if (pu.userID == 0) {
		if (gpilot_attempt_restore (pu, pfd, device, context) == FALSE) {
			gpilot_gui_warning_dialog (_("Use gnomecc to configure PDA"));
		}
	} else {
		/* FIXME: here we should offer to create a profile for the pilot,
		   bug # 8218 */
		gpilot_gui_warning_dialog (_("Unknown PDA - no PDA matches ID %ld\n"
					     "Use gnomecc to configure gnome-pilot"),pu.userID);
	}
}

/*
 * If there are events for the cradle, this executes them,
 * closes the connection and returns.
 * Returns TRUE if connection should be closed afterwards, FALSE
 * is sync should continue
 */
static gboolean 
do_cradle_events (int pfd,
		 GPilotContext *context,
		 struct PilotUser *pu,
		 GPilotDevice *device) 
{
	GList *events, *it;
	gboolean ret = TRUE;
	gchar *pilot_username;
	
	/* elements in events freed by gpc_request_purge calls
	   in dbus_notify_completion */
	events = gpc_queue_load_requests_for_cradle (device->name);
	
	g_message (_("Device %s has %d events"), device->name, g_list_length (events));
	
	/* if no events, return FALSE */
	if (!events)
		return FALSE;
	
	it = events;
	
	while (it) {
		GPilotRequest *req;
		req = it->data;
		switch (req->type) {
		case GREQ_SET_USERINFO:
			g_message (_("Setting userinfo..."));
			/* convert username from UTF-8 to pilot charset */
			convert_ToPilotChar_WithCharset ("UTF-8",
					req->parameters.set_userinfo.user_id,
					strlen(req->parameters.set_userinfo.user_id),
					&pilot_username, NULL);
			
			g_snprintf (pu->username,127,"%s", pilot_username);
			g_free(pilot_username);
			pu->userID = req->parameters.set_userinfo.pilot_id;
			dlp_WriteUserInfo (pfd,pu);
			if (req->parameters.set_userinfo.continue_sync) {
				g_message (_("Sync continues"));
				ret = FALSE;
			}
			dbus_notify_completion (&req);
			break;
		case GREQ_GET_SYSINFO: {
			struct SysInfo sysinfo;
			struct CardInfo cardinfo;

			dlp_ReadStorageInfo (pfd, 0, &cardinfo);
			dlp_ReadSysInfo (pfd, &sysinfo);
			dbus_notify_sysinfo (device->name,
					      sysinfo,
					      cardinfo, 
					      &req);
			dbus_notify_completion (&req);
		}
		break;
		case GREQ_GET_USERINFO:
			g_message (_("Getting userinfo..."));
			dbus_notify_userinfo (*pu,&req);
			dbus_notify_completion (&req);
			break;
		case GREQ_NEW_USERINFO:
			/* FIXME: this is to set the new and return the old (or something) 
			   g_message ("getting & setting userinfo");
			   g_snprintf (pu->username,127,"%s",req->parameters.set_userinfo.user_id);
			   pu->userID = req->parameters.set_userinfo.pilot_id;
			   dlp_WriteUserInfo (pfd,pu);
			   dbus_notify_completion (&req);
			*/
			break;
		default:
			g_warning ("%s:%d: *** type = %d",__FILE__,__LINE__,req->type);
			g_assert_not_reached ();
			break;
		}

		it = g_list_next (it);
	}

	return ret;
}
/**************************/

/*
  This executes a sync for a pilot.

  If first does some printing to the stdout and some logging to the
  pilot, so the dudes can see what is going on. Afterwards, it does
  the initial synchronization operations (which is handling file
  installs, restores, specific conduits runs). This function (in
  manager.c) returns a boolean, whic may abort the entire
  synchronization.

  If it does not, a function in manager.c will be called depending of
  the default_sync_action setting for the pilot (you know, synchronize
  vs copy to/from blablabla).

 */
static void 
do_sync (int pfd,   
	GPilotContext *context,
	struct PilotUser *pu,
	GPilotPilot *pilot, 
	GPilotDevice *device)
{
	GList *conduit_list, *backup_conduit_list, *file_conduit_list;
	GnomePilotSyncStamp stamp;
	char *pilot_name;

	pilot_name = pilot_name_from_id (pu->userID,context);

	gpilot_load_conduits (context,
			     pilot,
			     &conduit_list, 
			     &backup_conduit_list,
			     &file_conduit_list);
	stamp.sync_PC_Id=context->sync_PC_Id;

	if (device->type == PILOT_DEVICE_NETWORK) {
		g_message (_("NetSync request detected, synchronizing PDA"));
	} else if (device->type == PILOT_DEVICE_BLUETOOTH) {
		g_message (_("Bluetooth request detected, synchronizing PDA"));
	} else {
		g_message (_("HotSync button pressed, synchronizing PDA"));
	}
	g_message (_("PDA ID is %ld, name is %s, owner is %s"),
		  pu->userID,
		  pilot->name,
		  pu->username);
  
	/* Set a log entry in the pilot */
	{
		char hostname[64];
		gpilot_add_log_entry (pfd,"gnome-pilot v.%s\n",VERSION);
		if (gethostname (hostname,63)==0)
			gpilot_add_log_entry (pfd,_("On host %s\n"),hostname);
		else
			gpilot_add_log_entry (pfd,_("On host %d\n"),stamp.sync_PC_Id);
	}

	/* first, run the initial operations, such as single conduit runs,
	   restores etc. If this returns True, continue with normal conduit running,
	   if False, don't proceed */
	if (gpilot_initial_synchronize_operations (pfd,
						   &stamp,
						   pu,
						   conduit_list,
						   backup_conduit_list,
						   file_conduit_list,
						   device,
						   context)) {
		gpilot_sync_default (pfd,&stamp,pu,
				     conduit_list,
				     backup_conduit_list,
				     file_conduit_list,
				     context);
		g_message (_("Synchronization ended")); 
		gpilot_add_log_entry (pfd,"Synchronization completed");
	} else {
		g_message (_("Synchronization ended early"));
		gpilot_add_log_entry (pfd,"Synchronization terminated");
	}

	write_sync_stamp (pilot,pfd,pu,stamp.sync_PC_Id,time (NULL));
  
	g_free (pilot_name);

	gpilot_unload_conduits (conduit_list);
	gpilot_unload_conduits (backup_conduit_list);
	gpilot_unload_conduits (file_conduit_list);
}

/*
 * This function handles when sync_device (...) encounters a known pilot
 */

static void
gpilot_syncing_known_pilot (GPilotPilot *pilot,
			    struct PilotUser pu,
			    int pfd,
			    GPilotDevice *device,
			    GPilotContext *context)
{
	struct stat buf; 
	int ret;

	iconv_t ic;

	if (pilot->pilot_charset == NULL ||
	    pilot->pilot_charset == '\0') {
		g_warning (_("No pilot_charset specified.  Using `%s'."),
		    GPILOT_DEFAULT_CHARSET);
		if (pilot->pilot_charset != NULL)
			g_free(pilot->pilot_charset);
		pilot->pilot_charset =
		    g_strdup(GPILOT_DEFAULT_CHARSET);
	}


	/* ensure configured pilot_charset is recognised
	 * by iconv, and override with warning if not.
	 */
	ic = iconv_open(pilot->pilot_charset, "UTF8");
	if (ic == ((iconv_t)-1)) {
		g_warning (_("`%s' is not a recognised iconv charset, "
			       "using `%s' instead."),
		    pilot->pilot_charset,
		    GPILOT_DEFAULT_CHARSET);
		g_free (pilot->pilot_charset);
		pilot->pilot_charset =
		    g_strdup(GPILOT_DEFAULT_CHARSET);
	} else {
		iconv_close(ic);
	}
	/* Set the environment variable PILOT_CHARSET,
	 * to support legacy conduits that don't use
	 * pilot_charset
	 */
	setenv("PILOT_CHARSET",
	    pilot->pilot_charset, 1);

	ret = stat (pilot->sync_options.basedir, &buf); 

	if (ret < 0 || !( S_ISDIR (buf.st_mode) && (buf.st_mode & (S_IRUSR | S_IWUSR |S_IXUSR))) ) {
		
		g_message ("Invalid basedir: %s", pilot->sync_options.basedir);
		gpilot_gui_warning_dialog (_("The base directory %s is invalid.\n"
					     "Please fix it or use gnomecc to choose another directory."),
					   pilot->sync_options.basedir);	
	} else {
		gboolean pwd_ok = TRUE;
		/* If pilot has password, check against the encrypted version
		   on the pilot */
		if (pilot->passwd) {
			char *pwd;
			
			pwd = g_strndup (pu.password, pu.passwordLength);
			if (g_ascii_strcasecmp (pilot->passwd, crypt (pwd, pilot->passwd)) != 0) {
				pwd_ok = FALSE;
				gpilot_gui_warning_dialog (_("Unknown PDA - no PDA matches ID %ld\n"
							     "Use gpilotd-control-applet to set PDA's ID"), pu.userID);
			}
			g_free (pwd);
		}
		
		if (pwd_ok) {
			do_sync (pfd, context, &pu, pilot, device);
		}
	}
}

/*
  sync_foreach is the first synchronization entry.

  It first connects to the device on which the signal was detected,
  then it tries to read the user info block from the pilot.

  Hereafter, if there are any events queued for the synchronizing
  cradle, execute them and stop the synchronization (note,
  do_cradle_events returns a bool, if this is FALSE, synchronization
  continues, as some cradle specific events also require a normal sync
  afterwards, eg. the REVIVE call)

  Anyways, if the sync continues, sync_foreach tries to match the
  pilot against the known pilots. If this fails, it should handle it
  intelligently, eg. if the id==0, ask if you want to restore a pilot.

  If the pilot is accepted (dude, there's even a password check!), it
  continues into do_sync, which does all the magic stuff.
*/
   
static int
known_usb_device(gchar *match_str)
{
	int i;
	for (i = 0; i < vendor_product_ids->len; i++) {
		if (!g_ascii_strncasecmp (match_str, 
			vendor_product_ids->pdata[i], 
			strlen (vendor_product_ids->pdata[i])))
			return i;
	}
	return -1;
}

static void
load_devices_xml (void) 
{
	xmlDoc *doc = NULL;
	xmlNode *root, *node;
	char *filename;

	if (vendor_product_ids)
		return;
	
	vendor_product_ids = g_ptr_array_new ();
	product_net = g_array_new (FALSE, FALSE, sizeof (gboolean));

	filename = g_build_filename (DEVICE_XML_DIR, "devices.xml", NULL);
	doc = xmlParseFile (filename);
	g_free (filename);
		
	if (!doc) {
		g_warning ("Unable to read device file at %s", filename);
		
		return;
	}

	root = xmlDocGetRootElement (doc);
	if (!root->name || strcmp ((char *) root->name, "device-list"))
		goto fail;
	
	for (node = root->children; node; node = node->next) {
		xmlChar *vendor, *product, *net;
		gboolean use_net;
		
		if (node->type != XML_ELEMENT_NODE)
			continue;

		if (!node->name || strcmp ((char *) node->name, "device")) {
			g_warning ("Invalid sub node %s", node->name != NULL ? (char *) node->name : "");

			continue;
		}
		
		vendor = xmlGetProp (node, (xmlChar *) "vendor_id");
		product = xmlGetProp (node, (xmlChar *) "product_id");

		if (!vendor || !product) {
			g_warning ("No vendor or product id");

			continue;
		}
		
		g_message ("Found %s, %s", vendor, product);
		g_ptr_array_add (vendor_product_ids, g_strdup_printf ("Vendor=%s ProdID=%s", vendor, product));

		xmlFree (vendor);
		xmlFree (product);
		
		net = xmlGetProp (node, (xmlChar *) "use_net");
		use_net = !net || strcmp ((char *) net, "false");
		
		g_message ("Using net %s", use_net ? "TRUE" : "FALSE");
		g_array_append_val (product_net, use_net);

		xmlFree (net);
	}

 fail:
	xmlFreeDoc (doc);
}

static gboolean 
sync_device (GPilotDevice *device, GPilotContext *context)
{
	GPilotPilot *pilot;
	int connect_error;
	struct PilotUser pu;
	struct SysInfo ps;
	int pfd;
	
	g_assert (context != NULL);
	g_return_val_if_fail (device != NULL, FALSE);

	/* signal (SIGHUP,SIG_DFL); */
	pfd = pilot_connect (device,&connect_error);

	if (!connect_error) {
               /* connect succeeded, try to read the systeminfo */
               if (dlp_ReadSysInfo (pfd, &ps) < 0) {
                       /* no ? drop connection then */
                       g_warning (_("An error occurred while getting the PDA's system data"));
		       check_usb_config(device);


		/* connect succeeded, try to read the userinfo */
		} else if (dlp_ReadUserInfo (pfd,&pu) < 0) {
			/* no ? drop connection then */
			g_warning (_("An error occurred while getting the PDA's user data"));
		} else {
	
			/* If there are cradle specific events, handle them and stop */
			if (do_cradle_events (pfd,context,&pu,device)) {
				g_message (_("Completed events for device %s (%s)"),device->name,device->port);
			} else {
				/* No cradle events, validate pilot */
				pilot = gpilot_find_pilot_by_id (pu.userID,context->pilots);

				if (pilot == NULL) {
					/* Pilot is not known */
					gpilot_syncing_unknown_pilot (pu, pfd, device, context);
				} else {
					/* Pilot is known, make connect notifications */
					dbus_notify_connected (pilot->name,pu);				
					gpilot_syncing_known_pilot (pilot, pu, pfd, device, context);
					dbus_notify_disconnected (pilot->name);
				}				
			}
		}
		pilot_disconnect (pfd);
		/* now restart the listener.  fairly brute force
		 * approach, but ensures we re-initialise the listening
		 * socket correctly.  */
		if (device->type == PILOT_DEVICE_NETWORK ||
		    device->type == PILOT_DEVICE_BLUETOOTH) {
			/*TODO
                        reread_config = TRUE;
                        */
		}
	} else {
		if (connect_error==1) return FALSE; /* remove this device */
		else {
			if (device->type == PILOT_DEVICE_NETWORK ||
			    device->type == PILOT_DEVICE_BLUETOOTH) {
				/* fix broken pisock sockets */
                                /*TODO
                                reread_config = TRUE;
                                */
			}
			return TRUE;
		}
	}

	return TRUE;
}

static gboolean 
device_in (GIOChannel *io_channel, GIOCondition condition, GPilotContext *context)
{
	GPilotDevice *device;
	GList *element;
	gboolean result = TRUE;
	
	g_assert (context != NULL);
	
	element = g_list_find_custom (context->devices,
				     io_channel,
				     (GCompareFunc)device_equal_by_io);
	
	if (element == NULL || element->data == NULL) {
		g_warning ("cannot find device for active IO channel");
		return FALSE;
	}
	
	device = element->data; 
	if (context->paused) {
		return FALSE; 
	}	
	g_message (_("Woke on %s"), device->name);
	result = sync_device (device, context);
	
#ifdef WITH_IRDA
	if (device->type == PILOT_DEVICE_IRDA) {
		g_message ("Restarting irda funk...");
		gpilot_device_deinit (device);
		gpilot_device_init (device);
		monitor_channel (device, context);
		result = FALSE;
	}
#endif /* WITH_IRDA */
	
	return result;
}

static gboolean 
device_err (GIOChannel *io_channel, GIOCondition condition, GPilotContext *context)
{
	GPilotDevice *device;
	GList *element;
	char *tmp;
	
	g_assert (context != NULL);
	
	switch (condition) {
	case G_IO_IN: tmp = g_strdup_printf ("G_IO_IN"); break;
	case G_IO_OUT : tmp = g_strdup_printf ("G_IO_OUT"); break;
	case G_IO_PRI : tmp = g_strdup_printf ("G_IO_PRI"); break;
	case G_IO_ERR : tmp = g_strdup_printf ("G_IO_ERR"); break;
	case G_IO_HUP : tmp = g_strdup_printf ("G_IO_HUP"); break;
	case G_IO_NVAL: tmp = g_strdup_printf ("G_IO_NVAL"); break;
	default: tmp = g_strdup_printf ("unhandled port error"); break;
	}
	
	element = g_list_find_custom (context->devices,io_channel,(GCompareFunc)device_equal_by_io);
	
	if (element == NULL) {
		/* We most likely end here if the device has just been removed.
		   Eg. start gpilotd with a monitor on a XCopilot fake serial port,
		   kill xcopilot and watch things blow up as the device fails */
		g_warning ("Device error on some device, caught %s",tmp); 
		g_free (tmp);
		return FALSE;
	}
	
	device = element->data;
	
	gpilot_gui_warning_dialog ("Device error on %s (%s)\n"
				  "Caught %s", device->name, device->port, tmp); 
	g_warning ("Device error on %s (%s), caught %s", device->name, device->port, tmp);
	
	remove_device (context, device);
	g_free (tmp);
	
	return FALSE;
}

#ifdef WITH_NETWORK
static gboolean 
network_device_in (GIOChannel *io_channel, GIOCondition condition, GPilotContext *context)
{
	GPilotDevice *device;
	GList *element;
	gboolean result = TRUE;

	g_assert (context != NULL);
      
	element = g_list_find_custom (context->devices,
				     io_channel,
				     (GCompareFunc)device_equal_by_io);

	if (element==NULL || element->data == NULL) {
		g_warning ("cannot find device for active IO channel");
		return FALSE;
	}
	
	device = element->data; 
	if (context->paused) {
		return FALSE; 
	}	
	g_message (_("Woke on network: %s"),device->name);
	result = sync_device (device,context);

	return result;
}

static gboolean 
network_device_err (GIOChannel *io_channel, GIOCondition condition, GPilotContext *context)
{
	GPilotDevice *device;
	GList *element;
	char *tmp;
	
	g_assert (context != NULL);
	
	switch (condition) {
		case G_IO_IN: tmp = g_strdup_printf ("G_IO_IN"); break;
		case G_IO_OUT : tmp = g_strdup_printf ("G_IO_OUT"); break;
		case G_IO_PRI : tmp = g_strdup_printf ("G_IO_PRI"); break;
		case G_IO_ERR : tmp = g_strdup_printf ("G_IO_ERR"); break;
		case G_IO_HUP : tmp = g_strdup_printf ("G_IO_HUP"); break;
		case G_IO_NVAL: tmp = g_strdup_printf ("G_IO_NVAL"); break;
		default: tmp = g_strdup_printf ("unhandled port error"); break;
	}
	
	element = g_list_find_custom (context->devices,io_channel,(GCompareFunc)device_equal_by_io);
	
	if (element == NULL) {
		/* We most likely end here if the device has just been removed.
		   Eg. start gpilotd with a monitor on a XCopilot fake serial port,
		   kill xcopilot and watch things blow up as the device fails */
		g_warning ("Device error on some device, caught %s",tmp); 
		g_free (tmp);
		return FALSE;
	}
	
	device = element->data;

	gpilot_gui_warning_dialog ("Device error on %s, caught %s",
	    device->name, tmp);
	g_warning ("Device error on %s, caught %s",device->name, tmp);

	remove_device (context, device);
	g_free (tmp);
	
	return FALSE;
}
#endif /* WITH_NETWORK */

#ifdef WITH_USB_VISOR
static gboolean 
visor_devices_timeout (gpointer data) 
{
	GPilotContext *context = data;
	GPilotDevice *device;
	GList *l;
	int i, use_sysfs;
	static int devfs_warning = 0;
	gboolean visor_exists = FALSE, visor_net = TRUE;
	char *usbdevicesfile_str ="/proc/bus/usb/devices";
	char line[256]; /* this is more than enough to fit any line from 
			 * /proc/bus/usb/devices */
	FILE *f;
	gchar *fcontent;
	gchar *fname;
	gchar *vend_id; 
	gchar *prod_id;
	gchar *to_match;
	GError *error = NULL;
	regex_t regex_pattern;

	GDir *topdir;
	const gchar *entry;
	gchar *sysfs_dir_name = "/sys/bus/usb/devices/";
	gchar *f_vend = "idVendor";
	gchar *f_prod = "idProduct";


	g_assert (context != NULL);

	if (context->paused) 
		return FALSE;

	load_devices_xml ();
#ifdef linux
	/* choose a method for searching for known USB devices:
	 * If we can't find sysfs, second choice is
	 * legacy /proc/bus/usb/devices.
	 */
	use_sysfs = 1; /* default */
	topdir = g_dir_open (sysfs_dir_name, 0, &error);
	if (!topdir) {
		use_sysfs = 0;
		f = fopen (usbdevicesfile_str, "r");
		if (!f)
			f = fopen ("/proc/bus/usb/devices_please-use-sysfs-instead", "r");
		if (!f) {
			if (!devfs_warning) {
				devfs_warning = 1;
				char *str = g_strdup_printf (
				    _("Failed to find directory %s or read file %s.  "
					"Check that usbfs or sysfs is mounted."),
				    sysfs_dir_name,
				    usbdevicesfile_str);
				g_warning ("%s", str);
				g_free (str);
			}
			return TRUE; /* can't proceed */
		}
		devfs_warning = 0;
	}
	if (use_sysfs) {
		/* This regex allows 99 root-hubs and a bus-depth of 6 tiers for
		 * each root-hub. Each hub can have 99 ports. Refer to "Bus
		 * Topology" in the USB 2.0 specs and the sysfs structures of
		 * Linux USB for further explanation */
		regcomp (&regex_pattern,
		    "^[[:digit:]]{1,2}[-][[:digit:]]{1,2}([.][[:digit:]]{1,2}){0,5}$",
		    REG_EXTENDED | REG_NOSUB); 

		entry = g_dir_read_name (topdir);
		while ((entry != NULL) && (!visor_exists)) {
			if (!regexec (&regex_pattern, entry, 0, 0, 0)){
				fname = g_build_filename (sysfs_dir_name, entry,
				    f_vend, NULL);
				if (!g_file_get_contents (fname, &fcontent,
					NULL, &error)){
					g_warning ("%s", &*error->message);
					regfree (&regex_pattern);
					g_free (fname);
					g_dir_close (topdir);
					return TRUE;
				}
				vend_id = g_strndup (fcontent, 4);
				g_free (fname);
				g_free (fcontent);

				fname = g_build_filename (sysfs_dir_name, entry,
				    f_prod, NULL);
				if (!g_file_get_contents (fname, &fcontent,
					NULL, &error)){
					g_warning ("%s", &*error->message);
					regfree (&regex_pattern);
					g_free (fname);
					g_free (vend_id);
					g_dir_close (topdir);
					return TRUE;
				}
				prod_id = g_strndup (fcontent, 4);
				g_free (fname);
				g_free (fcontent);

				to_match = g_strconcat ("Vendor=", vend_id,
				    " ProdID=", prod_id, NULL);
				i = known_usb_device(to_match);
				if(i != -1) {
					visor_exists = TRUE;
					visor_net = g_array_index (
					    product_net, gboolean, i);
				}
				g_free (vend_id);
				g_free (prod_id);
				g_free (to_match);
			}
			entry = g_dir_read_name (topdir);
		}
		g_dir_close (topdir);
		regfree (&regex_pattern);
	} else {
		/* non sysfs branch... read /proc/bus/usb/devices */
		while (fgets (line, 255, f) != NULL && !visor_exists) {
			if (line[0] != 'P')
				continue;
			i = known_usb_device(line + 4); /* line + strlen("P:  ") */
			if (i != -1) {
				visor_exists = TRUE;
				visor_net = g_array_index (
				    product_net, gboolean, i);
			}
		}
	
		fclose (f);
	}
	
#else
#if defined(sun) && defined (__SVR4) /*for solaris */
	/* On Solaris we always try to sync.  This isn't
	 * a great solution, but does enable syncing.  We
	 * should use dbus on Solaris when it is working
	 * well.  See bug #385444.
	 */
       visor_exists = TRUE;
#endif /* defined(sun) && defined (__SVR4) */
#endif /* linux */
	if (visor_exists) {
		l = context->devices;
		while (l) {
			device = l->data;
			if (device->type == PILOT_DEVICE_USB_VISOR) {
				if (!visor_net)
					device->type = PILOT_DEVICE_SERIAL;

				/* just try to synch.  Until I can talk to 
				 * the kernel guys this is the best way to 
                                 * go. */
				sync_device (device, context);
				sleep(1);

				if (!visor_net)
					device->type = PILOT_DEVICE_USB_VISOR;
				break; /* don't try to sync any more devices! */
			}
			l = l->next;
		}
	}

	return TRUE;
}
#endif /* WITH_USB_VISOR */

static void
monitor_channel (GPilotDevice *dev, GPilotContext *context) 
{
	g_assert (context != NULL);
	
	if (dev->type == PILOT_DEVICE_SERIAL
	    || dev->type == PILOT_DEVICE_IRDA) {
		dev->in_handle = g_io_add_watch (dev->io,
						G_IO_IN,
						(GIOFunc)device_in,
						(gpointer)context);
		dev->err_handle = g_io_add_watch (dev->io,
						 G_IO_ERR|G_IO_PRI|G_IO_HUP|G_IO_NVAL,
						 (GIOFunc)device_err,
						 (gpointer)context);
	} else if (dev->type == PILOT_DEVICE_NETWORK ||
	    dev->type == PILOT_DEVICE_BLUETOOTH) {
#ifdef WITH_NETWORK
		dev->in_handle = g_io_add_watch (dev->io,
						G_IO_IN,
						(GIOFunc)network_device_in,
						(gpointer)context);
		dev->err_handle = g_io_add_watch (dev->io,
						 G_IO_ERR|G_IO_PRI|G_IO_HUP|G_IO_NVAL,
						 (GIOFunc)network_device_err,
						 (gpointer)context);
#else /* WITH_NETWORK */
		g_assert_not_reached ();
#endif /* WITH_NETWORK */
	} if (dev->type == PILOT_DEVICE_USB_VISOR) {
		if(udev_initialised) {
			/* handled by hal callbacks */
			dev->device_exists = FALSE;
		} else {
#ifdef WITH_USB_VISOR
#if defined(linux) || (defined(sun) && defined(__SVR4))
			/* We want to watch for a new recognised USB device
			 * once per context. */
			if (visor_timeout_id == -1) {
				visor_timeout_id = g_timeout_add (2000,
				    visor_devices_timeout, context);
			}
#else /* linux or solaris */
			g_assert_not_reached ();
#endif /* linux or solaris */
#endif /* WITH_USB_VISOR */
		}
	}

	if (dev->type == PILOT_DEVICE_NETWORK) {
                g_message (_("Watching %s (network)"), dev->name);
        } else if (dev->type == PILOT_DEVICE_BLUETOOTH) {
                g_message (_("Watching %s (bluetooth)"), dev->name);
        } else {
                g_message (_("Watching %s (%s)"), dev->name, dev->port);
        }
}

guint32
pilot_id_from_name (const gchar   *name,
                    GPilotContext *context)
{
        GList *pilot;
        pilot = g_list_find_custom (context->pilots, (gpointer)name,
                                    (GCompareFunc)match_pilot_and_name);
        if(pilot)  
                return ((GPilotPilot*)pilot->data)->pilot_id;
        return 0;
}

gchar*
pilot_name_from_id (guint32 id,
                    GPilotContext *context)
{
        GList *pilot;
        pilot = g_list_find_custom (context->pilots, (gpointer)&id,
                                   (GCompareFunc)match_pilot_userID);

        if(pilot)
                return g_strdup (((GPilotPilot*)pilot->data)->name);

        return NULL;
}

GQuark
gpilot_daemon_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("gpilot_daemon_error");
        }

        return ret;
}

#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
gpilot_daemon_error_get_type (void)
{
        static GType etype = 0;

        if (etype == 0) {
                static const GEnumValue values[] = {
                        ENUM_ENTRY (GPILOT_DAEMON_ERROR_GENERAL, "GeneralError"),
                        { 0, 0, 0 }
                };

                g_assert (GPILOT_DAEMON_NUM_ERRORS == G_N_ELEMENTS (values) - 1);

                etype = g_enum_register_static ("GpilotDaemonError", values);
        }

        return etype;
}

static void
dbus_notify_paused (gboolean on_ff)
{
        if (gdbus_connection == NULL) return;
        g_dbus_connection_emit_signal (gdbus_connection, NULL,
                                       GP_DBUS_PATH, GP_DBUS_INTERFACE,
                                       "Paused",
                                       g_variant_new ("(b)", on_ff),
                                       NULL);
}

/*
Example:
dbus-send --session --dest=org.gnome.GnomePilot \
--type=method_call --print-reply \
/org/gnome/GnomePilot/Daemon \
org.gnome.GnomePilot.Daemon.Pause boolean:true
*/
gboolean
gpilot_daemon_pause (GpilotDaemon   *daemon,
                     gboolean        on_off,
                     GError        **error)
{
        GpilotDaemonPrivate *priv;

        priv = daemon->priv;

        if (priv->gpilotd_context->paused == on_off)
                return TRUE;

        priv->gpilotd_context->paused = on_off;

        dbus_notify_paused (on_off);
        if (priv->gpilotd_context->paused) {
                g_list_foreach (priv->gpilotd_context->devices,
                                (GFunc)gpilot_daemon_pause_device,
                                NULL);
        } else {
                kill (getpid (), SIGHUP);
        }

        return TRUE;
}

/*
Example:
dbus-send --session --dest=org.gnome.GnomePilot \
--type=method_call --print-reply \
/org/gnome/GnomePilot/Daemon \
org.gnome.GnomePilot.Daemon.RereadConfig
*/
gboolean
gpilot_daemon_reread_config (GpilotDaemon   *daemon,
                             GError        **error)
{
        GpilotDaemonPrivate *priv;

        priv = daemon->priv;

        g_message (_("Shutting down devices"));
        gpilot_context_free (priv->gpilotd_context);
        g_message (_("Rereading configuration..."));
        gpilot_context_init_user (priv->gpilotd_context);
        g_list_foreach (priv->gpilotd_context->devices, (GFunc)monitor_channel, priv->gpilotd_context);

        return TRUE;
}

/* A no-operation call, used by client to occasionally
   check to see if the daemon has blown up
Example:
dbus-send --session --dest=org.gnome.GnomePilot \
--type=method_call --print-reply \
/org/gnome/GnomePilot/Daemon \
org.gnome.GnomePilot.Daemon.Noop
*/
gboolean
gpilot_daemon_noop (GpilotDaemon   *daemon)
{
        return TRUE;
}

/* request operations */
/*
Example:
dbus-send --session --dest=org.gnome.GnomePilot \
--type=method_call --print-reply \
/org/gnome/GnomePilot/Daemon \
org.gnome.GnomePilot.Daemon.RequestInstall \
string:'MyPDA' string:'/tmp/1' string:'/tmp/1' \
uint32:0 uint32:0
*/
gboolean
gpilot_daemon_request_install (GpilotDaemon   *daemon,
                               const char     *pilot_name,
                               const char     *filename,
                               const char     *description,
                               GNOME_Pilot_Survival survival,
                               unsigned long   timeout,
                               unsigned long  *handle,
                               GError        **error)
{
        GpilotDaemonPrivate *priv;
        GPilotRequest        req;
        gboolean             ret;
        
        ret = FALSE;
        priv = daemon->priv;

        LOG (("request_install(pilot_name=%s (%d),filename=%s,survival=%d,timeout=%lu)",                 
            pilot_name,       
            pilot_id_from_name (pilot_name, priv->gpilotd_context),
            filename,
            survival,
            timeout));

        req.type = GREQ_INSTALL;
        req.pilot_id = pilot_id_from_name (pilot_name, priv->gpilotd_context);

        if(req.pilot_id == 0) {
                g_set_error (error,
                             GPILOT_DAEMON_ERROR,
                             GPILOT_DAEMON_ERROR_GENERAL,
                             "Unknow pilot %s",
                             pilot_name);
                goto out;
        }
        if(access (filename, R_OK)) {
                g_set_error (error,
                             GPILOT_DAEMON_ERROR,
                             GPILOT_DAEMON_ERROR_GENERAL,
                             "Missing file %s",
                             filename);
                goto out;
        }

        req.timeout = survival==GNOME_Pilot_PERSISTENT?0:timeout;
        req.cradle = NULL;
        req.client_id = g_strdup ("");
        req.parameters.install.filename = g_strdup (filename);
        req.parameters.install.description = g_strdup (description);

        *handle = gpc_queue_store_request (req);

        ret = TRUE;
 out:
        return ret;
}

/*
Example:
dbus-send --session --dest=org.gnome.GnomePilot \
--type=method_call --print-reply \
/org/gnome/GnomePilot/Daemon \
org.gnome.GnomePilot.Daemon.RequestRestore \
string:'MyPDA' string:'/tmp/dir1' \
uint32:0 uint32:0
*/
gboolean
gpilot_daemon_request_restore (GpilotDaemon   *daemon,
                               const char     *pilot_name,
                               const char     *directory,
                               GNOME_Pilot_Survival survival,
                               unsigned long   timeout,
                               unsigned long  *handle,
                               GError        **error)
{
        GpilotDaemonPrivate *priv;
        GPilotRequest        req;
        gboolean             ret;
        
        ret = FALSE;
        priv = daemon->priv;

        LOG (("request_restore(pilot_name=%s (%d), directory=%s,survival=%d,timeout=%lu)",
              pilot_name,
              pilot_id_from_name (pilot_name, priv->gpilotd_context),
              directory,
              survival,
              timeout));

        req.type = GREQ_RESTORE;
        req.pilot_id = pilot_id_from_name (pilot_name, priv->gpilotd_context);
        if(req.pilot_id == 0) {
                g_set_error (error,
                             GPILOT_DAEMON_ERROR,
                             GPILOT_DAEMON_ERROR_GENERAL,
                             "Unknow pilot %s",
                             pilot_name);
                goto out;
        }
        if (!g_file_test (directory, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR)) {
                g_set_error (error,
                             GPILOT_DAEMON_ERROR,
                             GPILOT_DAEMON_ERROR_GENERAL,
                             "Directory %s not accessible",
                             directory);
                goto out;
        }

        req.timeout = survival==GNOME_Pilot_PERSISTENT?0:timeout;
        req.cradle = NULL;
        req.client_id = g_strdup ("");

        req.parameters.restore.directory = g_strdup(directory);

        *handle = gpc_queue_store_request (req);

        ret = TRUE;
 out:
        return ret;
}

/*
Example:
dbus-send --session --dest=org.gnome.GnomePilot \
--type=method_call --print-reply \
/org/gnome/GnomePilot/Daemon \
org.gnome.GnomePilot.Daemon.RequestConduit \
string:'MyPDA' string:'Test' \
uint32:0 uint32:0 uint32:0
*/
gboolean
gpilot_daemon_request_conduit (GpilotDaemon   *daemon,
                               const char     *pilot_name,
                               const char     *conduit_name,
                               GNOME_Pilot_ConduitOperation operation,
                               GNOME_Pilot_Survival survival,
                               unsigned long   timeout,
                               unsigned long  *handle,
                               GError        **error)
{
        GpilotDaemonPrivate *priv;
        GPilotRequest        req;
        gboolean             ret;
        
        ret = FALSE;
        priv = daemon->priv;

        LOG (("request_conduit(pilot=%s (%d), conduit=%s)",
              pilot_name,
              pilot_id_from_name (pilot_name, priv->gpilotd_context),
              conduit_name));

        req.pilot_id = pilot_id_from_name (pilot_name, priv->gpilotd_context);
        if(req.pilot_id == 0) {
                g_set_error (error,
                             GPILOT_DAEMON_ERROR,
                             GPILOT_DAEMON_ERROR_GENERAL,
                             "Unknow pilot %s",
                             pilot_name);
                goto out;
        }

        req.type = GREQ_CONDUIT;
        req.timeout = survival==GNOME_Pilot_PERSISTENT?0:timeout;
        req.cradle = NULL;
        req.client_id = g_strdup("");

        req.parameters.conduit.name = g_strdup (conduit_name);
        switch (operation) {
        case GNOME_Pilot_SYNCHRONIZE:
                req.parameters.conduit.how = GnomePilotConduitSyncTypeSynchronize; break;
        case GNOME_Pilot_COPY_FROM_PILOT:
                req.parameters.conduit.how = GnomePilotConduitSyncTypeCopyFromPilot; break;
        case GNOME_Pilot_COPY_TO_PILOT:
                req.parameters.conduit.how = GnomePilotConduitSyncTypeCopyToPilot; break;
        case GNOME_Pilot_MERGE_FROM_PILOT:
                req.parameters.conduit.how = GnomePilotConduitSyncTypeMergeFromPilot; break;
        case GNOME_Pilot_MERGE_TO_PILOT:
                req.parameters.conduit.how = GnomePilotConduitSyncTypeMergeToPilot; break;
        case GNOME_Pilot_CONDUIT_DEFAULT:
                req.parameters.conduit.how = GnomePilotConduitSyncTypeCustom; break;
        default:
                g_set_error (error,
                             GPILOT_DAEMON_ERROR,
                             GPILOT_DAEMON_ERROR_GENERAL,
                             "Unknow operation %d",
                             operation);
                break;
        }

        *handle = gpc_queue_store_request (req);

        ret = TRUE;
 out:
        return ret;
}

/* Example:
dbus-send --session --dest=org.gnome.GnomePilot \
--type=method_call --print-reply \
/org/gnome/GnomePilot/Daemon \
org.gnome.GnomePilot.Daemon.RemoveRequest \
uint32:1
*/
gboolean
gpilot_daemon_remove_request (GpilotDaemon   *daemon,
                              unsigned long   handle,
                              GError        **error)
{
        gpc_queue_purge_request_point (handle/65535, handle%65535);
        return TRUE;
}


/* information operations */
/* Example:
dbus-send --session --dest=org.gnome.GnomePilot \
--type=method_call --print-reply \
/org/gnome/GnomePilot/Daemon \
org.gnome.GnomePilot.Daemon.GetSystemInfo \
string:'Cradle' uint32:0 uint32:0
*/
gboolean
gpilot_daemon_get_system_info (GpilotDaemon   *daemon,
                               const char     *cradle,
                               GNOME_Pilot_Survival survival,
                               unsigned long   timeout,
                               unsigned long  *handle,
                               GError        **error)
{
        GpilotDaemonPrivate *priv;
        GPilotRequest        req;
        gboolean             ret;
        
        ret = FALSE;
        priv = daemon->priv;

        LOG (("get_system_info(cradle=%s,survival=%d,timeout=%lu)",
              cradle, survival, timeout));

        req.type = GREQ_GET_SYSINFO;
        req.timeout = survival==GNOME_Pilot_PERSISTENT?0:timeout;
        req.pilot_id = 0;
        req.cradle = g_strdup (cradle);
        req.client_id = g_strdup ("");

        *handle = gpc_queue_store_request (req);
        ret = TRUE;
 
        return ret;
}

/* Example:
dbus-send --session --dest=org.gnome.GnomePilot \
--type=method_call --print-reply \
/org/gnome/GnomePilot/Daemon \
org.gnome.GnomePilot.Daemon.GetUsers
*/
gboolean
gpilot_daemon_get_users (GpilotDaemon   *daemon,
                         char         ***users,
                         GError        **error)
{
        char *username;

        g_return_val_if_fail (GPILOT_IS_DAEMON (daemon), FALSE);
        
        LOG (("get_users()"));

        username = daemon->priv->gpilotd_context->user->username;
        if (IS_STR_SET (username)) {
                *users = g_new (char *, 2);
                (*users)[0] = g_strdup (username);
                (*users)[1] = NULL;
        }
        return TRUE;
}

/* Example:
dbus-send --session --dest=org.gnome.GnomePilot \
--type=method_call --print-reply \
/org/gnome/GnomePilot/Daemon \
org.gnome.GnomePilot.Daemon.GetCradles
*/
gboolean
gpilot_daemon_get_cradles (GpilotDaemon   *daemon,
                           char         ***cradles,
                           GError        **error)
{
        int    i;
        GList *l;

        g_return_val_if_fail (GPILOT_IS_DAEMON (daemon), FALSE);
        
        LOG (("get_cradles()"));

        l = daemon->priv->gpilotd_context->devices;

        *cradles = g_new (char *, g_list_length (l)+1);
        for(i=0; i < g_list_length (l); i++) {
                GPilotDevice *device = GPILOT_DEVICE (g_list_nth (l, i)->data);
                (*cradles)[i] = g_strdup (device->name);
        }
        (*cradles)[g_list_length (l)] = NULL;

        return TRUE;
}

/* Example:
dbus-send --session --dest=org.gnome.GnomePilot \
--type=method_call --print-reply \
/org/gnome/GnomePilot/Daemon \
org.gnome.GnomePilot.Daemon.GetPilots
*/
gboolean
gpilot_daemon_get_pilots (GpilotDaemon   *daemon,
                          char         ***pilots,
                          GError        **error)
{
        int    i;
        GList *l;

        g_return_val_if_fail (GPILOT_IS_DAEMON (daemon), FALSE);
        
        LOG (("get_pilots()"));

        l = daemon->priv->gpilotd_context->pilots;

        *pilots = g_new (char *, g_list_length (l)+1);
        for(i=0; i < g_list_length (l); i++) {
                GPilotPilot *pilot = GPILOT_PILOT (g_list_nth (l, i)->data);
                (*pilots)[i] = g_strdup (pilot->name);
        }
        (*pilots)[g_list_length (l)] = NULL;

        return TRUE;
}

/* Example:
dbus-send --session --dest=org.gnome.GnomePilot \
--type=method_call --print-reply \
/org/gnome/GnomePilot/Daemon \
org.gnome.GnomePilot.Daemon.GetPilotIDs
*/
gboolean
gpilot_daemon_get_pilot_ids (GpilotDaemon   *daemon,
                             GPtrArray     **pilots,
                             GError        **error)
{
        int    i;
        GList *l;

        g_return_val_if_fail (GPILOT_IS_DAEMON (daemon), FALSE);
        
        LOG (("get_pilot_ids()"));

        if (pilots == NULL)
                return FALSE;

        *pilots = g_ptr_array_new ();
        l = daemon->priv->gpilotd_context->pilots;

        for(i=0; i < g_list_length (l); i++) {
                GPilotPilot *pilot = GPILOT_PILOT (g_list_nth (l, i)->data);
                g_ptr_array_add (*pilots, GINT_TO_POINTER (pilot->pilot_id));
        }

        return TRUE;
}

/* Example:
dbus-send --session --dest=org.gnome.GnomePilot \
--type=method_call --print-reply \
/org/gnome/GnomePilot/Daemon \
org.gnome.GnomePilot.Daemon.GetPilotsByUserName \
string:'foo'
*/
gboolean
gpilot_daemon_get_pilots_by_user_name (GpilotDaemon   *daemon,
                                       const char     *username,
                                       char         ***pilots,
                                       GError        **error)
{
        int    i;
        GList *l;
        GList *matches;

        g_return_val_if_fail (GPILOT_IS_DAEMON (daemon), FALSE);
        
        LOG (("get_pilots_by_user_name(%s)", username));

        if (pilots == NULL)
                return FALSE;

        l = daemon->priv->gpilotd_context->pilots;

        matches = NULL;
        for(i=0; i < g_list_length (l); i++) {
                GPilotPilot *pilot = GPILOT_PILOT (g_list_nth (l, i)->data);
                if (g_str_equal (pilot->pilot_username, username)) {
                        LOG (("match on %s", pilot->pilot_username));
                        matches = g_list_append (matches, pilot->name);
                }
        }

        *pilots = g_new (char *, g_list_length (matches)+1);
        for(i=0; i < g_list_length (matches); i++) {
                (*pilots)[i] = g_strdup (g_list_nth (matches, i)->data);
        }
        (*pilots)[g_list_length (matches)] = NULL;

        return TRUE;
}

/* Example:
dbus-send --session --dest=org.gnome.GnomePilot \
--type=method_call --print-reply \
/org/gnome/GnomePilot/Daemon \
org.gnome.GnomePilot.Daemon.GetPilotsByUserLogin
string:'foo'
*/
gboolean
gpilot_daemon_get_pilots_by_user_login (GpilotDaemon   *daemon,
                                        const char     *uid,
                                        char         ***pilots,
                                        GError        **error)
{
        gchar   *username;
        gboolean ret;
        struct passwd *pwdent;

        g_return_val_if_fail (GPILOT_IS_DAEMON (daemon), FALSE);
        
        LOG (("get_pilots_by_user_login(%s)", uid));

        /* find the pwdent matching the login */
        username = NULL;
        setpwent ();
        do {
                pwdent = getpwent ();
                if (pwdent) {
                        if(strcmp (pwdent->pw_name,uid) == 0) {
                                username = strdup (pwdent->pw_gecos);
                                pwdent = NULL; /* end the loop */
                        }
                }
        } while (pwdent);
        endpwent();

        /* no luck ? */
        if(!username) {
                LOG (("no realname for %s", uid));
                return TRUE;
        }

        /* FIXME: uhm, is this use of username safe ? 
           or should it be CORBA::strdup'ed ? */
        ret = gpilot_daemon_get_pilots_by_user_name (daemon, username, pilots, error);
        g_free (username);
        return ret ;
}

gboolean
gpilot_daemon_get_user_name_by_pilot_name (GpilotDaemon   *daemon,
                                           const char     *pilot_name,
                                           char          **username,
                                           GError        **error)
{
        LOG (("FIXME %s:%d get_user_name_by_pilot_name", __FILE__, __LINE__));

        /* FIXME: not implemented yet */
        return TRUE;
}

gboolean
gpilot_daemon_get_user_login_by_pilot_name (GpilotDaemon   *daemon,
                                            const char     *pilot_name,
                                            char          **uid,
                                            GError        **error)
{
        LOG (("FIXME %s:%d get_user_login_by_pilot_name", __FILE__, __LINE__));

        /* FIXME: not implemented yet */
        return TRUE;
}

/* Example:
dbus-send --session --dest=org.gnome.GnomePilot \
--type=method_call --print-reply \
/org/gnome/GnomePilot/Daemon \
org.gnome.GnomePilot.Daemon.GetPilotBaseDir \
string:'MyPDA'
*/
gboolean
gpilot_daemon_get_pilot_base_dir (GpilotDaemon   *daemon,
                                  const char     *pilot_name,
                                  char          **base_dir,
                                  GError        **error)
{
        int    i;
        GList *l;

        g_return_val_if_fail (GPILOT_IS_DAEMON (daemon), FALSE);
        
        LOG (("get_pilot_base_dir()"));

        if (base_dir == NULL)
                return FALSE;

        l = daemon->priv->gpilotd_context->pilots;

        for(i=0; i < g_list_length (l); i++) {
                GPilotPilot *pilot = GPILOT_PILOT (g_list_nth (l, i)->data);
                if( g_str_equal (pilot->name, pilot_name)) {
                        LOG(("match on %s", pilot->name));
                        *base_dir = g_strdup (pilot->sync_options.basedir);
                }
        }

        return TRUE;
}

/* Example:
dbus-send --session --dest=org.gnome.GnomePilot \
--type=method_call --print-reply \
/org/gnome/GnomePilot/Daemon \
org.gnome.GnomePilot.Daemon.GetPilotIdFromName \
string:'MyPDA'
*/
gboolean
gpilot_daemon_get_pilot_id_from_name (GpilotDaemon   *daemon,
                                      const char     *pilot_name,
                                      guint          *pilot_id,
                                      GError        **error)
{
        LOG(("get_user_pilot_id_from_name"));

        *pilot_id = pilot_id_from_name (pilot_name,
                                        daemon->priv->gpilotd_context);

        return TRUE;
}

/* Example:
dbus-send --session --dest=org.gnome.GnomePilot \
--type=method_call --print-reply \
/org/gnome/GnomePilot/Daemon \
org.gnome.GnomePilot.Daemon.GetPilotNameFromId \
uint32:10001
*/
gboolean
gpilot_daemon_get_pilot_name_from_id (GpilotDaemon   *daemon,
                                      guint           pilot_id,
                                      char          **pilot_name,
                                      GError        **error)
{
        LOG(("get_pilot_name_from_id(id=%d)", pilot_id));
                                       
        *pilot_name = pilot_name_from_id (pilot_id,
                                          daemon->priv->gpilotd_context);
        return TRUE;
}

/* Example:
dbus-send --session --dest=org.gnome.GnomePilot \
--type=method_call --print-reply \
/org/gnome/GnomePilot/Daemon \
org.gnome.GnomePilot.Daemon.GetDatabasesFromCache \
string:'MyPDA'
*/
gboolean
gpilot_daemon_get_databases_from_cache (GpilotDaemon   *daemon,
                                        const char     *pilot_name,
                                        char         ***databases,
                                        GError        **error)
{
        int       i;
        gboolean  ret;
        guint32   pilot_id;
        GKeyFile *kfile;
        gsize     num_bases;
        char    **arr;

        g_return_val_if_fail (GPILOT_IS_DAEMON (daemon), FALSE);
        
        LOG (("get_databases_from_cache(...)"));

        ret = FALSE;
        if (databases == NULL)
                return FALSE;

        pilot_id = pilot_id_from_name (pilot_name,
                                       daemon->priv->gpilotd_context);

        if (pilot_id == 0) {
                g_set_error (error,
                             GPILOT_DAEMON_ERROR,
                             GPILOT_DAEMON_ERROR_GENERAL,
                             "Unknown pilot %s",
                             pilot_name);
                goto out;
        }

        kfile = get_pilot_cache_kfile (pilot_id);
        arr = g_key_file_get_string_list (kfile, "Databases",
                                          "databases", &num_bases, NULL);
        *databases = g_new (char *, num_bases + 1);
        for (i=0; i<num_bases; i++) {
                (*databases)[i] = g_strdup (arr[i]);
        }
        (*databases)[num_bases] = NULL;

        ret = TRUE;
        g_strfreev (arr);
 out:
        return TRUE;
}

/* admin operations */
/* Example:
dbus-send --session --dest=org.gnome.GnomePilot \
--type=method_call --print-reply \
/org/gnome/GnomePilot/Daemon \
org.gnome.GnomePilot.Daemon.GetUserInfo \
string:'Cradle' uint32:0 uint32:0
*/
gboolean
gpilot_daemon_get_user_info (GpilotDaemon   *daemon,
                             const char     *cradle,
                             GNOME_Pilot_Survival survival,
                             unsigned long   timeout,
                             unsigned long  *handle,
                             GError        **error)
{
        GpilotDaemonPrivate *priv;
        GPilotRequest        req;
        gboolean             ret;
        
        ret = FALSE;
        priv = daemon->priv;

        LOG (("get_user_info(cradle=%s,survival=%d,timeout=%lu)",
             cradle, survival, timeout));

        req.type = GREQ_GET_USERINFO;
        req.timeout = survival==GNOME_Pilot_PERSISTENT?0:timeout;
        req.pilot_id = 0;
        req.cradle = g_strdup (cradle);
        req.client_id = g_strdup ("");

        *handle = gpc_queue_store_request (req);
        ret = TRUE;

        return ret;
}

/* Example:
dbus-send --session --dest=org.gnome.GnomePilot \
--type=method_call --print-reply \
/org/gnome/GnomePilot/Daemon \
org.gnome.GnomePilot.Daemon.SetUserInfo \
string:'Cradle' boolean:true uint32:0 \
uint32:0 uint32:1111 username:'foo' 
*/
gboolean
gpilot_daemon_set_user_info (GpilotDaemon   *daemon,
                             const char     *cradle,
                             gboolean        continue_sync,
                             GNOME_Pilot_Survival survival,
                             unsigned long   timeout,
                             unsigned long   uid,
                             const char     *username,
                             unsigned long  *handle,
                             GError        **error)
{
        GpilotDaemonPrivate *priv;
        gboolean             ret;
        
        ret = FALSE;
        priv = daemon->priv;

        LOG (("set_user_info(cradle=%s,survival=%d,timeout=%lu",
            cradle,survival,timeout));
        LOG (("              device = %s,", cradle));
        LOG (("              user_id = %lu,", uid));
        LOG (("              user    = %s)", username));

        *handle = gpc_queue_store_set_userinfo_request (
                        survival==GNOME_Pilot_PERSISTENT?0:timeout, 
                        cradle,
                        "",
                        username,
                        uid,
                        continue_sync);
        ret = TRUE;

        return ret;
}

void
dbus_notify_connected (const gchar     *pilot_id,
                       struct PilotUser user_info)
{
        gchar *username;
        if (gdbus_connection == NULL) return;

        username = g_strdup (user_info.username);
        g_dbus_connection_emit_signal (gdbus_connection, NULL,
                                       GP_DBUS_PATH, GP_DBUS_INTERFACE,
                                       "Connected",
                                       g_variant_new ("(sus)", pilot_id,
                                                      (guint32) user_info.userID,
                                                      username),
                                       NULL);
        g_free (username);
}

void
dbus_notify_disconnected (const gchar *pilot_id)
{
        if (gdbus_connection == NULL) return;
        g_dbus_connection_emit_signal (gdbus_connection, NULL,
                                       GP_DBUS_PATH, GP_DBUS_INTERFACE,
                                       "Disconnected",
                                       g_variant_new ("(s)", pilot_id),
                                       NULL);
}

void
dbus_notify_completion (GPilotRequest **req)
{
        gchar *pilot_name;

        g_return_if_fail (req != NULL);
        g_return_if_fail (*req != NULL);
        if (gdbus_connection == NULL) return;

        pilot_name = ((*req)->cradle) ? g_strdup ((*req)->cradle) : g_strdup ("");
        g_dbus_connection_emit_signal (gdbus_connection, NULL,
                                       GP_DBUS_PATH, GP_DBUS_INTERFACE,
                                       "RequestCompleted",
                                       g_variant_new ("(su)", pilot_name,
                                                      (*req)->handle),
                                       NULL);
        gpc_queue_purge_request (req);
        g_free (pilot_name);
}

void
dbus_notify_userinfo (struct PilotUser    user_info,
                      GPilotRequest     **req)
{
        gchar *username;
        if (gdbus_connection == NULL) return;

        convert_FromPilotChar_WithCharset ("UTF-8",
                        user_info.username,
                        strlen (user_info.username),
                        &username, NULL);
        username = g_strdup (user_info.username);

        g_dbus_connection_emit_signal (gdbus_connection, NULL,
                                       GP_DBUS_PATH, GP_DBUS_INTERFACE,
                                       "UserInfoRequested",
                                       g_variant_new ("(sus)", (*req)->cradle,
                                                      (guint32) user_info.userID,
                                                      username),
                                       NULL);
        g_free (username);
}

void
dbus_notify_sysinfo (const gchar      *pilot_id,
                     struct SysInfo    sysinfo,
                     struct CardInfo   cardinfo,
                     GPilotRequest   **req)
{
        if (gdbus_connection == NULL) return;

        g_dbus_connection_emit_signal (gdbus_connection, NULL,
                                       GP_DBUS_PATH, GP_DBUS_INTERFACE,
                                       "SysInfoRequested",
                                       g_variant_new ("(suuussuu)", pilot_id,
                                                      (guint32)(cardinfo.romSize / 1024),
                                                      (guint32)(cardinfo.ramSize / 1024),
                                                      (guint32)(cardinfo.ramFree / 1024),
                                                      cardinfo.name ? cardinfo.name : "",
                                                      cardinfo.manufacturer ? cardinfo.manufacturer : "",
                                                      (guint32) cardinfo.creation,
                                                      (guint32) sysinfo.romVersion),
                                       NULL);
}

void
dbus_notify_conduit_start (const gchar              *pilot_id,
                           GnomePilotConduit        *conduit,
                           GnomePilotConduitSyncType type)
{
        gchar *name;
        gchar *database;

        if (gdbus_connection == NULL) return;

        name = gnome_pilot_conduit_get_name (conduit);
        if (GNOME_IS_PILOT_CONDUIT_STANDARD (conduit))
                database = g_strdup (gnome_pilot_conduit_standard_get_db_name (GNOME_PILOT_CONDUIT_STANDARD (conduit)));
        else
                database = g_strdup (_("(unknown DB)"));

        g_dbus_connection_emit_signal (gdbus_connection, NULL,
                                       GP_DBUS_PATH, GP_DBUS_INTERFACE,
                                       "ConduitStart",
                                       g_variant_new ("(sss)", pilot_id, name, database),
                                       NULL);
        g_free (name);
        g_free (database);
}

void
dbus_notify_conduit_end (const gchar       *pilot_id,
                         GnomePilotConduit *conduit)
{
        gchar *name;
        if (gdbus_connection == NULL) return;

        name = gnome_pilot_conduit_get_name (conduit);
        g_dbus_connection_emit_signal (gdbus_connection, NULL,
                                       GP_DBUS_PATH, GP_DBUS_INTERFACE,
                                       "ConduitEnd",
                                       g_variant_new ("(ss)", pilot_id, name),
                                       NULL);
        g_free (name);
}

void
dbus_notify_conduit_progress (const gchar       *pilot_id,
                              GnomePilotConduit *conduit,
                              guint32            current,
                              guint32            total)
{
        gchar *name;
        if (gdbus_connection == NULL) return;

        name = gnome_pilot_conduit_get_name (conduit);
        g_dbus_connection_emit_signal (gdbus_connection, NULL,
                                       GP_DBUS_PATH, GP_DBUS_INTERFACE,
                                       "ConduitProgress",
                                       g_variant_new ("(ssuu)", pilot_id, name,
                                                      current, total),
                                       NULL);
        g_free (name);
}

void
dbus_notify_overall_progress (const gchar    *pilot_id,
                              guint32         current,
                              guint32         total)
{
        if (gdbus_connection == NULL) return;
        g_dbus_connection_emit_signal (gdbus_connection, NULL,
                                       GP_DBUS_PATH, GP_DBUS_INTERFACE,
                                       "OverallProgress",
                                       g_variant_new ("(suu)", pilot_id,
                                                      current, total),
                                       NULL);
}

static void
dbus_notify_message (const gchar       *pilot_id,
                     GnomePilotConduit *conduit,
                     const gchar       *msg,
                     const gchar       *signal_name)
{
        gchar *name;
        if (gdbus_connection == NULL) return;

        if (GNOME_IS_PILOT_CONDUIT (conduit))
                name = gnome_pilot_conduit_get_name (conduit);
        else
                name = g_strdup ("");

        g_dbus_connection_emit_signal (gdbus_connection, NULL,
                                       GP_DBUS_PATH, GP_DBUS_INTERFACE,
                                       signal_name,
                                       g_variant_new ("(sss)", pilot_id,
                                                      IS_STR_SET (name) ? name : "",
                                                      msg),
                                       NULL);
        g_free (name);
}

void
dbus_notify_conduit_error (const gchar       *pilot_id,
                           GnomePilotConduit *conduit,
                           const gchar       *message)
{
        dbus_notify_message (pilot_id, conduit, message, "ConduitError");
}

void
dbus_notify_conduit_message (const gchar       *pilot_id,
                             GnomePilotConduit *conduit,
                             const gchar       *message)
{
        dbus_notify_message (pilot_id, conduit, message, "ConduitMessage");
}

void
dbus_notify_daemon_message (const gchar       *pilot_id,
                            GnomePilotConduit *conduit,
                            const gchar       *message)
{
        dbus_notify_message (pilot_id, conduit, message, "DaemonMessage");
}

void
dbus_notify_daemon_error (const gchar       *pilot_id,
                          const gchar       *message)
{
        dbus_notify_message (pilot_id, NULL, message, "DaemonError");
}

static gboolean
register_daemon (GpilotDaemon *daemon)
{
        GError *error = NULL;

        daemon->priv->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
        if (daemon->priv->connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting session bus: %s", error->message);
                        g_error_free (error);
                }
                exit (1);
        }

        /* Store globally for signal emission from static functions */
        gdbus_connection = daemon->priv->connection;

        /* TODO: register D-Bus interface using gdbus-codegen generated skeleton */

        return TRUE;
}

static void
gpilot_daemon_class_init (GpilotDaemonClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gpilot_daemon_finalize;

        signals [CONNECTED] = g_signal_new ("connected",
                                            G_TYPE_FROM_CLASS (object_class),
                                            G_SIGNAL_RUN_LAST,
                                            G_STRUCT_OFFSET (GpilotDaemonClass, connected),
                                            NULL,
                                            NULL,
                                            gp_marshal_VOID__STRING_UINT_STRING,
                                            G_TYPE_NONE,
                                            3,
                                            G_TYPE_STRING,
                                            G_TYPE_UINT,
                                            G_TYPE_STRING);
        signals [DISCONNECTED] = g_signal_new ("disconnected",
                                            G_TYPE_FROM_CLASS (object_class),
                                            G_SIGNAL_RUN_LAST,
                                            G_STRUCT_OFFSET (GpilotDaemonClass, disconnected),
                                            NULL,
                                            NULL,
                                            gp_marshal_VOID__STRING,
                                            G_TYPE_NONE,
                                            1,
                                            G_TYPE_STRING);
        signals [REQUEST_COMPLETED] = g_signal_new ("request-completed",
                                            G_TYPE_FROM_CLASS (object_class),
                                            G_SIGNAL_RUN_LAST,
                                            G_STRUCT_OFFSET (GpilotDaemonClass, request_completed),
                                            NULL,
                                            NULL,
                                            gp_marshal_VOID__STRING_UINT,
                                            G_TYPE_NONE,
                                            2,
                                            G_TYPE_STRING,
                                            G_TYPE_UINT);
        signals [USERINFO_REQUESTED] = g_signal_new ("user-info-requested",
                                            G_TYPE_FROM_CLASS (object_class),
                                            G_SIGNAL_RUN_LAST,
                                            G_STRUCT_OFFSET (GpilotDaemonClass, user_info_requested),
                                            NULL,
                                            NULL,
                                            gp_marshal_VOID__STRING_UINT_STRING,
                                            G_TYPE_NONE,
                                            3,
                                            G_TYPE_STRING,
                                            G_TYPE_UINT,
                                            G_TYPE_STRING);
        signals [SYSINFO_REQUESTED] = g_signal_new ("sys-info-requested",
                                            G_TYPE_FROM_CLASS (object_class),
                                            G_SIGNAL_RUN_LAST,
                                            G_STRUCT_OFFSET (GpilotDaemonClass, sys_info_requested),
                                            NULL,
                                            NULL,
                                            gp_marshal_VOID__STRING_UINT_UINT_UINT_STRING_STRING_UINT_UINT,
                                            G_TYPE_NONE,
                                            8,
                                            G_TYPE_STRING,
                                            G_TYPE_UINT,
                                            G_TYPE_UINT,
                                            G_TYPE_UINT,
                                            G_TYPE_STRING,
                                            G_TYPE_STRING,
                                            G_TYPE_UINT,
                                            G_TYPE_UINT);
        signals [CONDUIT_START] = g_signal_new ("conduit-start",
                                            G_TYPE_FROM_CLASS (object_class),
                                            G_SIGNAL_RUN_LAST,
                                            G_STRUCT_OFFSET (GpilotDaemonClass, conduit_start),
                                            NULL,
                                            NULL,
                                            gp_marshal_VOID__STRING_STRING_STRING,
                                            G_TYPE_NONE,
                                            3,
                                            G_TYPE_STRING,
                                            G_TYPE_STRING,
                                            G_TYPE_STRING);
        signals [CONDUIT_PROGRESS] = g_signal_new ("conduit-progress",
                                            G_TYPE_FROM_CLASS (object_class),
                                            G_SIGNAL_RUN_LAST,
                                            G_STRUCT_OFFSET (GpilotDaemonClass, conduit_progress),
                                            NULL,
                                            NULL,
                                            gp_marshal_VOID__STRING_STRING_UINT_UINT,
                                            G_TYPE_NONE,
                                            4,
                                            G_TYPE_STRING,
                                            G_TYPE_STRING,
                                            G_TYPE_UINT,
                                            G_TYPE_UINT);
        signals [CONDUIT_END] = g_signal_new ("conduit-end",
                                            G_TYPE_FROM_CLASS (object_class),
                                            G_SIGNAL_RUN_LAST,
                                            G_STRUCT_OFFSET (GpilotDaemonClass, conduit_end),
                                            NULL,
                                            NULL,
                                            gp_marshal_VOID__STRING,
                                            G_TYPE_NONE,
                                            1,
                                            G_TYPE_STRING);
        signals [OVERALL_PROGRESS] = g_signal_new ("overall-progress",
                                            G_TYPE_FROM_CLASS (object_class),
                                            G_SIGNAL_RUN_LAST,
                                            G_STRUCT_OFFSET (GpilotDaemonClass, overall_progress),
                                            NULL,
                                            NULL,
                                            gp_marshal_VOID__STRING_UINT_UINT,
                                            G_TYPE_NONE,
                                            3,
                                            G_TYPE_STRING,
                                            G_TYPE_UINT,
                                            G_TYPE_UINT);
        signals [DAEMON_MESSAGE] = g_signal_new ("daemon-message",
                                            G_TYPE_FROM_CLASS (object_class),
                                            G_SIGNAL_RUN_LAST,
                                            G_STRUCT_OFFSET (GpilotDaemonClass, daemon_message),
                                            NULL,
                                            NULL,
                                            gp_marshal_VOID__STRING_STRING_STRING,
                                            G_TYPE_NONE,
                                            3,
                                            G_TYPE_STRING,
                                            G_TYPE_STRING,
                                            G_TYPE_STRING);
        signals [DAEMON_ERROR] = g_signal_new ("daemon-error",
                                            G_TYPE_FROM_CLASS (object_class),
                                            G_SIGNAL_RUN_LAST,
                                            G_STRUCT_OFFSET (GpilotDaemonClass, daemon_error),
                                            NULL,
                                            NULL,
                                            gp_marshal_VOID__STRING_STRING,
                                            G_TYPE_NONE,
                                            2,
                                            G_TYPE_STRING,
                                            G_TYPE_STRING);
        signals [CONDUIT_MESSAGE] = g_signal_new ("conduit-message",
                                            G_TYPE_FROM_CLASS (object_class),
                                            G_SIGNAL_RUN_LAST,
                                            G_STRUCT_OFFSET (GpilotDaemonClass, conduit_message),
                                            NULL,
                                            NULL,
                                            gp_marshal_VOID__STRING_STRING_STRING,
                                            G_TYPE_NONE,
                                            3,
                                            G_TYPE_STRING,
                                            G_TYPE_STRING,
                                            G_TYPE_STRING);
        signals [CONDUIT_ERROR] = g_signal_new ("conduit-error",
                                            G_TYPE_FROM_CLASS (object_class),
                                            G_SIGNAL_RUN_LAST,
                                            G_STRUCT_OFFSET (GpilotDaemonClass, conduit_error),
                                            NULL,
                                            NULL,
                                            gp_marshal_VOID__STRING_STRING_STRING,
                                            G_TYPE_NONE,
                                            3,
                                            G_TYPE_STRING,
                                            G_TYPE_STRING,
                                            G_TYPE_STRING);
        signals [PAUSED] = g_signal_new ("paused",
                                            G_TYPE_FROM_CLASS (object_class),
                                            G_SIGNAL_RUN_LAST,
                                            G_STRUCT_OFFSET (GpilotDaemonClass, paused),
                                            NULL,
                                            NULL,
                                            gp_marshal_VOID__BOOLEAN,
                                            G_TYPE_NONE,
                                            1,
                                            G_TYPE_BOOLEAN);

        g_type_class_add_private (klass, sizeof (GpilotDaemonPrivate));

        /* D-Bus interface registration is now handled in register_daemon() via GDBus */
}

static void
gpilot_daemon_init (GpilotDaemon *daemon)
{
	gpilotd_gudev_init();
	udev_initialised = 1;
	
        daemon->priv = GPILOT_DAEMON_GET_PRIVATE (daemon);

        daemon->priv->gpilotd_context = gpilot_context_new ();
        gpilot_context_init_user (daemon->priv->gpilotd_context);

        g_list_foreach (daemon->priv->gpilotd_context->devices,
                        (GFunc)monitor_channel,
                        daemon->priv->gpilotd_context);
}

static void
gpilot_daemon_finalize (GObject *object)
{
        GpilotDaemon *daemon;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GPILOT_IS_DAEMON (object));

        daemon= GPILOT_DAEMON (object);

        g_return_if_fail (daemon->priv != NULL);

        if (daemon->priv->connection != NULL) {
                g_object_unref (daemon->priv->connection);
                gdbus_connection = NULL;
        }

        G_OBJECT_CLASS (gpilot_daemon_parent_class)->finalize (object);
}

GpilotDaemon*
gpilot_daemon_new ()
{
        if (daemon_object != NULL) {
                g_object_ref (daemon_object);
        } else {
                gboolean res;

                daemon_object = g_object_new (GPILOT_TYPE_DAEMON, NULL);
                g_object_add_weak_pointer (daemon_object,
                                           (gpointer *) &daemon_object);
                res = register_daemon (daemon_object);
                if (! res) {
                        g_object_unref (daemon_object);
                        return NULL;
                }

        }

        return GPILOT_DAEMON (daemon_object);
}
