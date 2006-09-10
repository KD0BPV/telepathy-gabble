/*
 * gabble-muc-channel.c - Source for GabbleMucChannel
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>

#include "gabble-muc-channel.h"
#include "gabble-muc-channel-signals-marshal.h"

#include "gabble-muc-channel-glue.h"

G_DEFINE_TYPE(GabbleMucChannel, gabble_muc_channel, G_TYPE_OBJECT)

/* signal enum */
enum
{
    CLOSED,
    GROUP_FLAGS_CHANGED,
    LOST_MESSAGE,
    MEMBERS_CHANGED,
    PASSWORD_FLAGS_CHANGED,
    PROPERTIES_CHANGED,
    PROPERTY_FLAGS_CHANGED,
    RECEIVED,
    SEND_ERROR,
    SENT,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _GabbleMucChannelPrivate GabbleMucChannelPrivate;

struct _GabbleMucChannelPrivate
{
  gboolean dispose_has_run;
};

#define GABBLE_MUC_CHANNEL_GET_PRIVATE(obj) \
    ((GabbleMucChannelPrivate *)obj->priv)

static void
gabble_muc_channel_init (GabbleMucChannel *self)
{
  GabbleMucChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_MUC_CHANNEL, GabbleMucChannelPrivate);

  self->priv = priv;

  /* allocate any data required by the object here */
}

static void gabble_muc_channel_dispose (GObject *object);
static void gabble_muc_channel_finalize (GObject *object);

static void
gabble_muc_channel_class_init (GabbleMucChannelClass *gabble_muc_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_muc_channel_class);

  g_type_class_add_private (gabble_muc_channel_class, sizeof (GabbleMucChannelPrivate));

  object_class->dispose = gabble_muc_channel_dispose;
  object_class->finalize = gabble_muc_channel_finalize;

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[GROUP_FLAGS_CHANGED] =
    g_signal_new ("group-flags-changed",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_muc_channel_marshal_VOID__UINT_UINT,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  signals[LOST_MESSAGE] =
    g_signal_new ("lost-message",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[MEMBERS_CHANGED] =
    g_signal_new ("members-changed",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_muc_channel_marshal_VOID__STRING_BOXED_BOXED_BOXED_BOXED_UINT_UINT,
                  G_TYPE_NONE, 7, G_TYPE_STRING, DBUS_TYPE_G_UINT_ARRAY, DBUS_TYPE_G_UINT_ARRAY, DBUS_TYPE_G_UINT_ARRAY, DBUS_TYPE_G_UINT_ARRAY, G_TYPE_UINT, G_TYPE_UINT);

  signals[PASSWORD_FLAGS_CHANGED] =
    g_signal_new ("password-flags-changed",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_muc_channel_marshal_VOID__UINT_UINT,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  signals[PROPERTIES_CHANGED] =
    g_signal_new ("properties-changed",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, (dbus_g_type_get_collection ("GPtrArray", (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, G_TYPE_VALUE, G_TYPE_INVALID)))));

  signals[PROPERTY_FLAGS_CHANGED] =
    g_signal_new ("property-flags-changed",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, (dbus_g_type_get_collection ("GPtrArray", (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INVALID)))));

  signals[RECEIVED] =
    g_signal_new ("received",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_muc_channel_marshal_VOID__UINT_UINT_UINT_UINT_UINT_STRING,
                  G_TYPE_NONE, 6, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  signals[SEND_ERROR] =
    g_signal_new ("send-error",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_muc_channel_marshal_VOID__UINT_UINT_UINT_STRING,
                  G_TYPE_NONE, 4, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  signals[SENT] =
    g_signal_new ("sent",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_muc_channel_marshal_VOID__UINT_UINT_STRING,
                  G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (gabble_muc_channel_class), &dbus_glib_gabble_muc_channel_object_info);
}

void
gabble_muc_channel_dispose (GObject *object)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (object);
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gabble_muc_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_muc_channel_parent_class)->dispose (object);
}

void
gabble_muc_channel_finalize (GObject *object)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (object);
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (gabble_muc_channel_parent_class)->finalize (object);
}



/**
 * gabble_muc_channel_acknowledge_pending_messages
 *
 * Implements D-Bus method AcknowledgePendingMessages
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_muc_channel_acknowledge_pending_messages (GabbleMucChannel *self,
                                                 const GArray *ids,
                                                 GError **error)
{
  return TRUE;
}


/**
 * gabble_muc_channel_add_members
 *
 * Implements D-Bus method AddMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_muc_channel_add_members (GabbleMucChannel *self,
                                const GArray *contacts,
                                const gchar *message,
                                GError **error)
{
  return TRUE;
}


/**
 * gabble_muc_channel_close
 *
 * Implements D-Bus method Close
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_muc_channel_close (GabbleMucChannel *self,
                          GError **error)
{
  return TRUE;
}


/**
 * gabble_muc_channel_get_all_members
 *
 * Implements D-Bus method GetAllMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_muc_channel_get_all_members (GabbleMucChannel *self,
                                    GArray **ret,
                                    GArray **ret1,
                                    GArray **ret2,
                                    GError **error)
{
  return TRUE;
}


/**
 * gabble_muc_channel_get_channel_type
 *
 * Implements D-Bus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_muc_channel_get_channel_type (GabbleMucChannel *self,
                                     gchar **ret,
                                     GError **error)
{
  return TRUE;
}


/**
 * gabble_muc_channel_get_group_flags
 *
 * Implements D-Bus method GetGroupFlags
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_muc_channel_get_group_flags (GabbleMucChannel *self,
                                    guint *ret,
                                    GError **error)
{
  return TRUE;
}


/**
 * gabble_muc_channel_get_handle
 *
 * Implements D-Bus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_muc_channel_get_handle (GabbleMucChannel *self,
                               guint *ret,
                               guint *ret1,
                               GError **error)
{
  return TRUE;
}


/**
 * gabble_muc_channel_get_handle_owners
 *
 * Implements D-Bus method GetHandleOwners
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_muc_channel_get_handle_owners (GabbleMucChannel *self,
                                      const GArray *handles,
                                      GArray **ret,
                                      GError **error)
{
  return TRUE;
}


/**
 * gabble_muc_channel_get_interfaces
 *
 * Implements D-Bus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_muc_channel_get_interfaces (GabbleMucChannel *self,
                                   gchar ***ret,
                                   GError **error)
{
  return TRUE;
}


/**
 * gabble_muc_channel_get_local_pending_members
 *
 * Implements D-Bus method GetLocalPendingMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_muc_channel_get_local_pending_members (GabbleMucChannel *self,
                                              GArray **ret,
                                              GError **error)
{
  return TRUE;
}


/**
 * gabble_muc_channel_get_members
 *
 * Implements D-Bus method GetMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_muc_channel_get_members (GabbleMucChannel *self,
                                GArray **ret,
                                GError **error)
{
  return TRUE;
}


/**
 * gabble_muc_channel_get_message_types
 *
 * Implements D-Bus method GetMessageTypes
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_muc_channel_get_message_types (GabbleMucChannel *self,
                                      GArray **ret,
                                      GError **error)
{
  return TRUE;
}


/**
 * gabble_muc_channel_get_password_flags
 *
 * Implements D-Bus method GetPasswordFlags
 * on interface org.freedesktop.Telepathy.Channel.Interface.Password
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_muc_channel_get_password_flags (GabbleMucChannel *self,
                                       guint *ret,
                                       GError **error)
{
  return TRUE;
}


/**
 * gabble_muc_channel_get_properties
 *
 * Implements D-Bus method GetProperties
 * on interface org.freedesktop.Telepathy.Properties
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_muc_channel_get_properties (GabbleMucChannel *self,
                                   const GArray *properties,
                                   GPtrArray **ret,
                                   GError **error)
{
  return TRUE;
}


/**
 * gabble_muc_channel_get_remote_pending_members
 *
 * Implements D-Bus method GetRemotePendingMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_muc_channel_get_remote_pending_members (GabbleMucChannel *self,
                                               GArray **ret,
                                               GError **error)
{
  return TRUE;
}


/**
 * gabble_muc_channel_get_self_handle
 *
 * Implements D-Bus method GetSelfHandle
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_muc_channel_get_self_handle (GabbleMucChannel *self,
                                    guint *ret,
                                    GError **error)
{
  return TRUE;
}


/**
 * gabble_muc_channel_list_pending_messages
 *
 * Implements D-Bus method ListPendingMessages
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_muc_channel_list_pending_messages (GabbleMucChannel *self,
                                          gboolean clear,
                                          GPtrArray **ret,
                                          GError **error)
{
  return TRUE;
}


/**
 * gabble_muc_channel_list_properties
 *
 * Implements D-Bus method ListProperties
 * on interface org.freedesktop.Telepathy.Properties
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_muc_channel_list_properties (GabbleMucChannel *self,
                                    GPtrArray **ret,
                                    GError **error)
{
  return TRUE;
}


/**
 * gabble_muc_channel_provide_password
 *
 * Implements D-Bus method ProvidePassword
 * on interface org.freedesktop.Telepathy.Channel.Interface.Password
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
void
gabble_muc_channel_provide_password (GabbleMucChannel *self,
                                     const gchar *password,
                                     DBusGMethodInvocation *context)
{
  return;
}


/**
 * gabble_muc_channel_remove_members
 *
 * Implements D-Bus method RemoveMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_muc_channel_remove_members (GabbleMucChannel *self,
                                   const GArray *contacts,
                                   const gchar *message,
                                   GError **error)
{
  return TRUE;
}


/**
 * gabble_muc_channel_send
 *
 * Implements D-Bus method Send
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_muc_channel_send (GabbleMucChannel *self,
                         guint type,
                         const gchar *text,
                         GError **error)
{
  return TRUE;
}


/**
 * gabble_muc_channel_set_properties
 *
 * Implements D-Bus method SetProperties
 * on interface org.freedesktop.Telepathy.Properties
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
void
gabble_muc_channel_set_properties (GabbleMucChannel *self,
                                   const GPtrArray *properties,
                                   DBusGMethodInvocation *context)
{
  return;
}

