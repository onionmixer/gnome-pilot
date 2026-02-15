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

#ifndef __GPILOT_DAEMON_H
#define __GPILOT_DAEMON_H

G_BEGIN_DECLS

#include <glib-object.h>
#include <gio/gio.h>

#include "gnome-pilot-structures.h"
#include "gnome-pilot-dbinfo.h"
#include "queue_io.h"

#define GPILOT_TYPE_DAEMON         (gpilot_daemon_get_type ())
#define GPILOT_DAEMON(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GPILOT_TYPE_DAEMON, GpilotDaemon))
#define GPILOT_DAEMON_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GPILOT_TYPE_DAEMON, GpilotDaemonClass))
#define GPILOT_IS_DAEMON(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GPILOT_TYPE_DAEMON))
#define GPILOT_IS_DAEMON_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GPILOT_TYPE_DAEMON))
#define GPILOT_DAEMON_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GPILOT_TYPE_DAEMON, GpilotDaemonClass))

#define LOG(x) g_message x

typedef struct GpilotDaemonPrivate GpilotDaemonPrivate;
typedef struct
{
        GObject            parent;
        GpilotDaemonPrivate *priv;
} GpilotDaemon;

typedef struct
{
        GObjectClass   parent_class;
        void          (* connected)          (GpilotDaemon   *gpilot,
                                              const char     *pilot_name,
                                              unsigned long  *uid,
                                              const char     *user);
        void          (* disconnected)       (GpilotDaemon   *gpilot,
                                              const char     *pilot_id);
        void          (* request_completed)  (GpilotDaemon   *gpilot,
                                              const char     *pilot_id,
                                              unsigned long   request_id);
        void          (* user_info_requested) (GpilotDaemon   *gpilot,
                                              const char     *device,
                                              unsigned long   uid,
                                              const char     *username);
        void          (* sys_info_requested) (GpilotDaemon   *gpilot,
                                              const char     *pilot_id,
                                              int             rom_size,
                                              int             ram_size,
                                              int             ram_free,
                                              const char     *name,
                                              const char     *manufacturer,
                                              int             creation,
                                              int             rom_version);
        void          (* conduit_start)      (GpilotDaemon   *gpilot,
                                              const char     *pilot_id,
                                              const char     *conduit,
                                              const char     *database);
        void          (* conduit_progress)   (GpilotDaemon   *gpilot,
                                              const char     *pilot_id,
                                              const char     *conduit,
                                              unsigned long   current,
                                              unsigned long   total);
        void          (* conduit_end)        (GpilotDaemon   *gpilot,
                                              const char     *pilot_id);
        void          (* overall_progress)   (GpilotDaemon   *gpilot,
                                              const char     *pilot_id,
                                              unsigned long   current,
                                              unsigned long   total);
        void          (* daemon_message)     (GpilotDaemon   *gpilot,
                                              const char     *pilot_id,
                                              const char     *conduit,
                                              const char     *message);
        void          (* daemon_error)       (GpilotDaemon   *gpilot,
                                              const char     *pilot_id,
                                              const char     *message);
        void          (* conduit_message)    (GpilotDaemon   *gpilot,
                                              const char     *pilot_id,
                                              const char     *conduit,
                                              const char     *message);
        void          (* conduit_error)      (GpilotDaemon   *gpilot,
                                              const char     *pilot_id,
                                              const char     *conduit,
                                              const char     *message);
        void          (* paused)             (GpilotDaemon   *gpilot,
                                              gboolean        on_ff);
} GpilotDaemonClass;

typedef struct {
        unsigned long  userID;
        gchar         *username;
} GNOME_Pilot_UserInfo;

typedef struct {
        long     romSize;
        long     ramSize;
        long     ramFree;
        char    *name;
        char    *manufacturer;
        long     creation;
        long     romVersion;
} GNOME_Pilot_SysInfo;

typedef enum
{
        GNOME_Pilot_IMMEDIATE,
        GNOME_Pilot_PERSISTENT
} GNOME_Pilot_Survival;

typedef enum
{
        GNOME_Pilot_SYNCHRONIZE,
        GNOME_Pilot_CONDUIT_DEFAULT,
        GNOME_Pilot_COPY_FROM_PILOT,
        GNOME_Pilot_COPY_TO_PILOT,
        GNOME_Pilot_MERGE_FROM_PILOT,
        GNOME_Pilot_MERGE_TO_PILOT
} GNOME_Pilot_ConduitOperation;

typedef enum
{
        GPILOT_DAEMON_ERROR_GENERAL,
        GPILOT_DAEMON_NUM_ERRORS
} GpilotDaemonError;

#define GPILOT_DAEMON_ERROR gpilot_daemon_error_quark ()

GType           gpilot_daemon_error_get_type    (void);
#define GPILOT_DAEMON_TYPE_ERROR (gpilot_daemon_error_get_type ())

GQuark          gpilot_daemon_error_quark       (void);
GType           gpilot_daemon_get_type          (void);
GpilotDaemon*   gpilot_daemon_new               (void);

/* exported to bus */

/* adm calls */
gboolean        gpilot_daemon_pause             (GpilotDaemon   *daemon,
                                                 gboolean        on_off,
                                                 GError        **error);
gboolean        gpilot_daemon_reread_config     (GpilotDaemon   *daemon,
                                                 GError        **error);

/* A no-operation call, used by client to occasionally
   check to see if the daemon has blown up */
gboolean        gpilot_daemon_noop              (GpilotDaemon   *daemon);
/* request operations */
gboolean        gpilot_daemon_request_install   (GpilotDaemon   *daemon,
                                                 const char     *pilot_name,
                                                 const char     *filename,
                                                 const char     *description,
                                                 GNOME_Pilot_Survival survival,
                                                 unsigned long   timeout,
                                                 unsigned long  *handle,
                                                 GError        **error);
gboolean        gpilot_daemon_request_restore   (GpilotDaemon   *daemon,
                                                 const char     *pilot_name,
                                                 const char     *directory,
                                                 GNOME_Pilot_Survival survival,
                                                 unsigned long   timeout,
                                                 unsigned long  *handle,
                                                 GError        **error);
gboolean        gpilot_daemon_request_conduit   (GpilotDaemon   *daemon,
                                                 const char     *pilot_name,
                                                 const char     *conduit_name,
                                                 GNOME_Pilot_ConduitOperation operation,
                                                 GNOME_Pilot_Survival survival,
                                                 unsigned long   timeout,
                                                 unsigned long  *handle,
                                                 GError        **error);
gboolean        gpilot_daemon_remove_request    (GpilotDaemon   *daemon,
                                                 unsigned long   handle,
                                                 GError        **error);
/* information operations */
gboolean        gpilot_daemon_get_system_info   (GpilotDaemon   *daemon,
                                                 const char     *cradle,
                                                 GNOME_Pilot_Survival survival,
                                                 unsigned long   timeout,
                                                 unsigned long  *handle,
                                                 GError        **error);
gboolean        gpilot_daemon_get_users         (GpilotDaemon   *daemon,
                                                 char         ***users,
                                                 GError        **error);
gboolean        gpilot_daemon_get_cradles       (GpilotDaemon   *daemon,
                                                 char         ***cradles,
                                                 GError        **error);
gboolean        gpilot_daemon_get_pilots        (GpilotDaemon   *daemon,
                                                 char         ***pilots,
                                                 GError        **error);
gboolean        gpilot_daemon_get_pilot_ids     (GpilotDaemon   *daemon,
                                                 GPtrArray     **pilots,
                                                 GError        **error);
gboolean        gpilot_daemon_get_pilots_by_user_name
                                                (GpilotDaemon   *daemon,
                                                 const char     *username,
                                                 char         ***pilots,
                                                 GError        **error);
gboolean        gpilot_daemon_get_pilots_by_user_login
                                                (GpilotDaemon   *daemon,
                                                 const char     *uid,
                                                 char         ***pilots,
                                                 GError        **error);
gboolean        gpilot_daemon_get_user_name_by_pilot_name
                                                (GpilotDaemon   *daemon,
                                                 const char     *pilot_name,
                                                 char          **username,
                                                 GError        **error);
gboolean        gpilot_daemon_get_user_login_by_pilot_name
                                                (GpilotDaemon   *daemon,
                                                 const char     *pilot_name,
                                                 char          **uid,
                                                 GError        **error);
gboolean        gpilot_daemon_get_pilot_base_dir
                                                (GpilotDaemon   *daemon,
                                                 const char     *pilot_name,
                                                 char          **base_dir,
                                                 GError        **error);
gboolean        gpilot_daemon_get_pilot_id_from_name
                                                (GpilotDaemon   *daemon,
                                                 const char     *pilot_name,
                                                 guint          *pilot_id,
                                                 GError        **error);
gboolean        gpilot_daemon_get_pilot_name_from_id
                                                (GpilotDaemon   *daemon,
                                                 guint           pilot_id,
                                                 char          **pilot_name,
                                                 GError        **error);
gboolean        gpilot_daemon_get_databases_from_cache
                                                (GpilotDaemon   *daemon,
                                                 const char     *pilot_name,
                                                 char         ***databases,
                                                 GError        **error);
/* admin operations */
gboolean        gpilot_daemon_get_user_info     (GpilotDaemon   *daemon,
                                                 const char     *cradle,
                                                 GNOME_Pilot_Survival survival,
                                                 unsigned long   timeout,
                                                 unsigned long  *handle,
                                                 GError        **error);
gboolean        gpilot_daemon_set_user_info     (GpilotDaemon   *daemon,
                                                 const char     *cradle,
                                                 gboolean        continue_sync,
                                                 GNOME_Pilot_Survival survival,
                                                 unsigned long   timeout,
                                                 unsigned long   uid,
                                                 const char     *username,
                                                 unsigned long  *handle,
                                                 GError        **error);
/* general functions */
guint32         pilot_id_from_name              (const gchar    *name,
                                                 GPilotContext  *context);
gchar*          pilot_name_from_id              (guint32         id,
                                                 GPilotContext  *context);

/* send dbus signals */
void            dbus_notify_connected           (const gchar    *pilot_id,
                                                 struct PilotUser user_info);
void            dbus_notify_disconnected        (const gchar    *pilot_id);
void            dbus_notify_completion          (GPilotRequest **req);
void            dbus_notify_userinfo            (struct PilotUser user_info,
                                                 GPilotRequest **req);
void            dbus_notify_sysinfo             (const gchar    *pilot_id,
                                                 struct SysInfo  sysinfo,
                                                 struct CardInfo cardinfo,
                                                 GPilotRequest **req);

void            dbus_notify_conduit_start       (const gchar    *pilot_id,
                                                 GnomePilotConduit *conduit,
                                                 GnomePilotConduitSyncType);
void            dbus_notify_conduit_end         (const gchar    *pilot_id,
                                                 GnomePilotConduit *conduit);
void            dbus_notify_conduit_error       (const gchar    *pilot_id,
                                                 GnomePilotConduit *conduit,
                                                 const gchar    *message);
void            dbus_notify_conduit_message     (const gchar    *pilot_id,
                                                 GnomePilotConduit *conduit,
                                                 const gchar    *message);
void            dbus_notify_conduit_progress    (const gchar    *pilot_id,
                                                 GnomePilotConduit *conduit,
                                                 guint32         current,
                                                 guint32         total);
void            dbus_notify_overall_progress    (const gchar    *pilot_id,
                                                 guint32         current,
                                                 guint32         total);
void            dbus_notify_daemon_message      (const gchar    *pilot_id,
                                                 GnomePilotConduit *conduit,
                                                 const gchar    *message);
void            dbus_notify_daemon_error        (const gchar    *pilot_id,
                                                 const gchar    *message);

G_END_DECLS

#endif /* __GPILOT_DAEMON_H */
