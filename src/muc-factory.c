/*
 * muc-factory.c - Source for GabbleMucFactory
 * Copyright (C) 2006 Collabora Ltd.
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

#include "config.h"
#include "muc-factory.h"

#define DBUS_API_SUBJECT_TO_CHANGE

#include <string.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <loudmouth/loudmouth.h>
#include <telepathy-glib/channel-factory-iface.h>
#include <telepathy-glib/interfaces.h>

#define DEBUG_FLAG GABBLE_DEBUG_MUC

#include "connection.h"
#include "conn-olpc.h"
#include "debug.h"
#include "disco.h"
#include "muc-channel.h"
#include "namespaces.h"
#include "presence-cache.h"
#include "roomlist-channel.h"
#include "text-mixin.h"
#include "tubes-channel.h"
#include "util.h"

static void gabble_muc_factory_iface_init (gpointer g_iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (GabbleMucFactory, gabble_muc_factory, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_FACTORY_IFACE,
      gabble_muc_factory_iface_init));

/* properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

typedef struct _GabbleMucFactoryPrivate GabbleMucFactoryPrivate;
struct _GabbleMucFactoryPrivate
{
  GabbleConnection *conn;

  LmMessageHandler *message_cb;
  LmMessageHandler *presence_cb;
  /* GUINT_TO_POINTER(room_handle) => (GabbleMucChannel *) */
  GHashTable *text_channels;
  /* GUINT_TO_POINTER(room_handle) => (GabbleTubesChannel *) */
  GHashTable *tubes_channels;
  GabbleRoomlistChannel *roomlist_channel;
  /* Tubes channel requests which will be satisfied when the corresponding
   * text channel is created.
   * GabbleMucChannel * => GabbleTubesChannel * */
  GHashTable *text_needed_for_tubes;
  GHashTable *disco_requests;

  gboolean dispose_has_run;
};

#define GABBLE_MUC_FACTORY_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_MUC_FACTORY, \
                                GabbleMucFactoryPrivate))

static GObject *gabble_muc_factory_constructor (GType type, guint n_props,
    GObjectConstructParam *props);

static void
gabble_muc_factory_init (GabbleMucFactory *fac)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);

  priv->text_channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_object_unref);
  priv->tubes_channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_object_unref);
  priv->text_needed_for_tubes = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, NULL);

  priv->disco_requests = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                                NULL, NULL);

  priv->message_cb = NULL;
  priv->presence_cb = NULL;

  priv->conn = NULL;
  priv->dispose_has_run = FALSE;
}

static GObject *
gabble_muc_factory_constructor (GType type, guint n_props,
                                GObjectConstructParam *props)
{
  GObject *obj;
  /* GabbleMucFactoryPrivate *priv; */

  obj = G_OBJECT_CLASS (gabble_muc_factory_parent_class)->
           constructor (type, n_props, props);
  /* priv = GABBLE_MUC_FACTORY_GET_PRIVATE (obj); */

  return obj;
}

static void
cancel_disco_request (gpointer key, gpointer value, gpointer user_data)
{
  GabbleDisco *disco = GABBLE_DISCO (user_data);
  GabbleDiscoRequest *request = (GabbleDiscoRequest *) key;

  gabble_disco_cancel_request (disco, request);
}

static void
gabble_muc_factory_dispose (GObject *object)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (object);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  tp_channel_factory_iface_close_all (TP_CHANNEL_FACTORY_IFACE (object));
  g_assert (priv->text_channels == NULL);
  g_assert (priv->tubes_channels == NULL);
  g_assert (priv->text_needed_for_tubes == NULL);

  g_hash_table_foreach (priv->disco_requests, cancel_disco_request,
      priv->conn->disco);
  g_hash_table_destroy (priv->disco_requests);

  if (G_OBJECT_CLASS (gabble_muc_factory_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_muc_factory_parent_class)->dispose (object);
}

static void
gabble_muc_factory_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (object);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_muc_factory_set_property (GObject      *object,
                                 guint         property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (object);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_muc_factory_class_init (GabbleMucFactoryClass *gabble_muc_factory_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_muc_factory_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_muc_factory_class,
      sizeof (GabbleMucFactoryPrivate));

  object_class->constructor = gabble_muc_factory_constructor;
  object_class->dispose = gabble_muc_factory_dispose;

  object_class->get_property = gabble_muc_factory_get_property;
  object_class->set_property = gabble_muc_factory_set_property;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
                                    "Gabble connection object that owns this "
                                    "MUC factory object.",
                                    GABBLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);
}

/**
 * muc_channel_closed_cb:
 *
 * Signal callback for when a MUC channel is closed. Removes the references
 * that MucFactory holds to them.
 */
static void
muc_channel_closed_cb (GabbleMucChannel *chan, gpointer user_data)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (user_data);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  TpHandle room_handle;

  if (priv->text_channels != NULL)
    {
      g_object_get (chan, "handle", &room_handle, NULL);

      DEBUG ("removing MUC channel with handle %d", room_handle);

      if (priv->tubes_channels != NULL)
        {
          GabbleTubesChannel *tubes;

          tubes = g_hash_table_lookup (priv->tubes_channels,
              GUINT_TO_POINTER (room_handle));
          if (tubes != NULL)
            gabble_tubes_channel_close (tubes);
        }

      g_hash_table_remove (priv->text_channels,
          GINT_TO_POINTER (room_handle));
    }
}

/**
 * tubes_channel_closed_cb:
 *
 * Signal callback for when a tubes channel is closed. Removes the references
 * that MucFactory holds to them.
 */
static void
tubes_channel_closed_cb (GabbleTubesChannel *chan, gpointer user_data)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (user_data);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  TpHandle room_handle;

  if (priv->tubes_channels != NULL)
    {
      g_object_get (chan, "handle", &room_handle, NULL);

      DEBUG ("removing MUC tubes channel with handle %d", room_handle);

      g_hash_table_remove (priv->tubes_channels,
          GINT_TO_POINTER (room_handle));
    }
}

static void
muc_ready_cb (GabbleMucChannel *chan,
              gpointer data)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (data);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  GabbleTubesChannel *tubes_chan;

  DEBUG ("chan=%p", chan);

  tp_channel_factory_iface_emit_new_channel (fac, (TpChannelIface *) chan,
      NULL);

  tubes_chan = g_hash_table_lookup (priv->text_needed_for_tubes, chan);
  if (tubes_chan != NULL)
    {
      g_hash_table_remove (priv->text_needed_for_tubes, chan);
      tp_channel_factory_iface_emit_new_channel (fac,
          (TpChannelIface *) tubes_chan, NULL);
    }
}

static void
muc_join_error_cb (GabbleMucChannel *chan,
                   GError *error,
                   gpointer data)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (data);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  GabbleTubesChannel *tubes_chan;

  DEBUG ("error->code=%u, error->message=\"%s\"", error->code, error->message);

  tp_channel_factory_iface_emit_channel_error (fac, (TpChannelIface *) chan,
      error, NULL);

  tubes_chan = g_hash_table_lookup (priv->text_needed_for_tubes, chan);
  if (tubes_chan != NULL)
    {
      g_hash_table_remove (priv->text_needed_for_tubes, chan);
      tp_channel_factory_iface_emit_channel_error (fac,
          (TpChannelIface *) tubes_chan, error, NULL);
    }
}

/**
 * new_muc_channel
 */
static GabbleMucChannel *
new_muc_channel (GabbleMucFactory *fac,
                 TpHandle handle,
                 gboolean invite_self,
                 TpHandle inviter,
                 const gchar *message)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  TpBaseConnection *conn = (TpBaseConnection *) priv->conn;
  GabbleMucChannel *chan;
  char *object_path;

  g_assert (g_hash_table_lookup (priv->text_channels,
        GINT_TO_POINTER (handle)) == NULL);

  if (invite_self)
    {
      g_assert (inviter == conn->self_handle);
      g_assert (message == NULL);
    }
  else
    {
      g_assert (inviter != 0);
      g_assert (message != NULL);
    }

  object_path = g_strdup_printf ("%s/MucChannel%u",
      conn->object_path, handle);

  DEBUG ("creating new chan, object path %s", object_path);

  chan = g_object_new (GABBLE_TYPE_MUC_CHANNEL,
      "connection", priv->conn,
       "object-path", object_path,
       "handle", handle,
       "invite-self", invite_self,
       "initiator-handle", inviter,
       NULL);

  g_signal_connect (chan, "closed", (GCallback) muc_channel_closed_cb, fac);

  g_hash_table_insert (priv->text_channels, GINT_TO_POINTER (handle), chan);

  g_free (object_path);

  g_signal_connect (chan, "ready", G_CALLBACK (muc_ready_cb), fac);
  g_signal_connect (chan, "join-error", G_CALLBACK (muc_join_error_cb),
                    fac);

  if (!invite_self)
    _gabble_muc_channel_handle_invited (chan, inviter, message);

  return chan;
}

/**
 * new_tubes_channel:
 *
 * Creates the GabbleTubesChannel object with the given parameters.
 */
static GabbleTubesChannel *
new_tubes_channel (GabbleMucFactory *fac,
                   TpHandle room,
                   GabbleMucChannel *muc)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  TpBaseConnection *conn = (TpBaseConnection *) priv->conn;
  GabbleTubesChannel *chan;
  char *object_path;

  g_assert (g_hash_table_lookup (priv->tubes_channels,
        GINT_TO_POINTER (room)) == NULL);

  object_path = g_strdup_printf ("%s/MucTubesChannel%u",
      conn->object_path, room);

  DEBUG ("creating new tubes chan, object path %s", object_path);

  chan = g_object_new (GABBLE_TYPE_TUBES_CHANNEL,
      "connection", priv->conn,
      "object-path", object_path,
      "handle", room,
      "handle-type", TP_HANDLE_TYPE_ROOM,
      "muc", muc,
      NULL);

  g_signal_connect (chan, "closed", (GCallback) tubes_channel_closed_cb, fac);

  g_hash_table_insert (priv->tubes_channels, GINT_TO_POINTER (room), chan);

  g_free (object_path);

  return chan;
}

static void
do_invite (GabbleMucFactory *fac,
           const gchar *room,
           TpHandle inviter_handle,
           const gchar *reason)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_ROOM);
  TpHandle room_handle;

  room_handle = tp_handle_ensure (room_repo, room, NULL, NULL);

  if (room_handle == 0)
    {
      DEBUG ("got a MUC invitation message with invalid room JID \"%s\"; "
          "ignoring", room);
      return;
    }

  if (g_hash_table_lookup (priv->text_channels,
        GUINT_TO_POINTER (room_handle)) == NULL)
    {
      new_muc_channel (fac, room_handle, FALSE, inviter_handle, reason);
    }
  else
    {
      DEBUG ("ignoring invite to room \"%s\"; we're already there", room);
    }

  tp_handle_unref (room_repo, room_handle);
}

struct DiscoInviteData {
    GabbleMucFactory *factory;
    gchar *reason;
    TpHandle inviter;
};

/**
 * obsolete_invite_disco_cb:
 *
 * Callback for disco request we fired upon encountering obsolete disco.
 * If the object is in fact MUC room, create a channel for it.
 */
static void
obsolete_invite_disco_cb (GabbleDisco *self,
                          GabbleDiscoRequest *request,
                          const gchar *jid,
                          const gchar *node,
                          LmMessageNode *query_result,
                          GError* error,
                          gpointer user_data)
{
  struct DiscoInviteData *data = (struct DiscoInviteData *) user_data;

  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (data->factory);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  LmMessageNode *identity;
  const char *category = NULL, *type = NULL;

  g_hash_table_remove (priv->disco_requests, request);

  if (error != NULL)
    {
      DEBUG ("ignoring obsolete invite to room '%s'; got disco error: %s",
          jid, error->message);
      goto out;
    }

  identity = lm_message_node_get_child (query_result, "identity");
  if (identity != NULL)
    {
      category = lm_message_node_get_attribute (identity, "category");
      type = lm_message_node_get_attribute (identity, "type");
    }

  if (tp_strdiff (category, "conference") ||
      tp_strdiff (type, "text"))
    {
      DEBUG ("obsolete invite request specified inappropriate jid '%s' "
          "(not a text conference); ignoring request", jid);
      goto out;
    }

  /* OK, it's MUC after all, create a new channel */
  do_invite (fac, jid, data->inviter, data->reason);

out:
  tp_handle_unref (contact_repo, data->inviter);
  g_free (data->reason);
  g_slice_free (struct DiscoInviteData, data);
}

static gboolean
process_muc_invite (GabbleMucFactory *fac,
                    LmMessage *message,
                    const gchar *from,
                    TpChannelTextSendError send_error)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  TpBaseConnection *conn = (TpBaseConnection *) priv->conn;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);

  LmMessageNode *x_node, *invite_node, *reason_node;
  const gchar *invite_from, *reason = NULL;
  TpHandle inviter_handle;
  gchar *room;

  /* does it have a muc subnode? */
  x_node = lm_message_node_get_child_with_namespace (message->node, "x",
      NS_MUC_USER);

  if (x_node == NULL)
    return FALSE;

  /* and an invitation? */
  invite_node = lm_message_node_get_child (x_node, "invite");

  if (invite_node == NULL)
    return FALSE;

  /* FIXME: do something with these? */
  if (send_error != GABBLE_TEXT_CHANNEL_SEND_NO_ERROR)
    {
      NODE_DEBUG (message->node, "got a MUC invitation message with a send "
          "error; ignoring");

      return TRUE;
    }

  invite_from = lm_message_node_get_attribute (invite_node, "from");
  if (invite_from == NULL)
    {
      NODE_DEBUG (message->node, "got a MUC invitation message with no JID; "
          "ignoring");

      return TRUE;
    }

  inviter_handle = tp_handle_ensure (contact_repo, invite_from,
      NULL, NULL);
  if (inviter_handle == 0)
    {
      NODE_DEBUG (message->node, "got a MUC invitation message with invalid "
          "inviter JID; ignoring");

      return TRUE;
    }

  reason_node = lm_message_node_get_child (invite_node, "reason");

  if (reason_node != NULL)
    reason = lm_message_node_get_value (reason_node);

  if (reason == NULL)
    reason = "";

  /* create the channel */
  room = gabble_remove_resource (from);
  do_invite (fac, room, inviter_handle, reason);
  g_free (room);

  tp_handle_unref (contact_repo, inviter_handle);

  return TRUE;
}

static gboolean
process_obsolete_invite (GabbleMucFactory *fac,
                         LmMessage *message,
                         const gchar *from,
                         const gchar *body,
                         TpChannelTextSendError send_error)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  TpBaseConnection *conn = (TpBaseConnection *) priv->conn;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);

  LmMessageNode *x_node;
  const gchar *room;
  TpHandle inviter_handle;
  GabbleDiscoRequest *request;
  struct DiscoInviteData *disco_udata;

  /* check for obsolete invite method */
  x_node = lm_message_node_get_child_with_namespace (message->node, "x",
      NS_X_CONFERENCE);
  if (x_node == NULL)
    return FALSE;

  /* this can only happen if the user sent an obsolete invite with another
   * client or something */
  if (send_error != GABBLE_TEXT_CHANNEL_SEND_NO_ERROR)
    {
      NODE_DEBUG (message->node, "got an obsolete MUC invitation message with "
          "a send error; ignoring");

      return TRUE;
    }

  /* the room JID is in x */
  room = lm_message_node_get_attribute (x_node, "jid");
  if (room == NULL)
    {
      NODE_DEBUG (message->node, "got a obsolete MUC invitation with no room "
          "JID; ignoring");

      return TRUE;
    }

  /* the inviter JID is in "from" */
  inviter_handle = tp_handle_ensure (contact_repo, from, NULL, NULL);
  if (inviter_handle == 0)
    {
      NODE_DEBUG (message->node, "got an obsolete MUC invitation message from "
          "an invalid JID; ignoring");

      return TRUE;
    }

  disco_udata = g_slice_new0 (struct DiscoInviteData);
  disco_udata->factory = fac;
  disco_udata->reason = g_strdup (body);
  disco_udata->inviter = inviter_handle;

  DEBUG ("received obsolete MUC invite from handle %u (%s), discoing room %s",
      inviter_handle, from, room);

  request = gabble_disco_request (priv->conn->disco, GABBLE_DISCO_TYPE_INFO,
      room, NULL, obsolete_invite_disco_cb, disco_udata, G_OBJECT (fac), NULL);

  if (request != NULL)
    {
      g_hash_table_insert (priv->disco_requests, request, NULL);
    }
  else
    {
      DEBUG ("obsolete MUC invite disco failed, freeing info");

      tp_handle_unref (contact_repo, inviter_handle);
      g_free (disco_udata->reason);
      g_slice_free (struct DiscoInviteData, disco_udata);
    }

  return TRUE;
}

/**
 * muc_factory_message_cb:
 *
 * Called by loudmouth when we get an incoming <message>.
 * We filter only groupchat and MUC messages, ignoring the rest.
 */
static LmHandlerResult
muc_factory_message_cb (LmMessageHandler *handler,
                        LmConnection *connection,
                        LmMessage *message,
                        gpointer user_data)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (user_data);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  TpBaseConnection *conn = (TpBaseConnection *) priv->conn;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_ROOM);

  const gchar *from, *body, *subject;
  time_t stamp;
  TpChannelTextMessageType msgtype;
  TpHandleRepoIface *handle_source;
  TpHandleType handle_type;
  TpHandle room_handle, handle;
  GabbleMucChannel *chan;
  gint state;
  TpChannelTextSendError send_error;
  gchar *room;
  LmMessageNode *subj_node;

  if (!gabble_text_mixin_parse_incoming_message (message, &from, &stamp,
        &msgtype, &body, &state, &send_error))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  if (conn_olpc_process_activity_properties_message (priv->conn, message,
        from))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  if (conn_olpc_process_activity_uninvite_message (priv->conn, message,
        from))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  if (process_muc_invite (fac, message, from, send_error))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  if (process_obsolete_invite (fac, message, from, body, send_error))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  /* check if a room with the jid exists */
  room = gabble_remove_resource (from);
  room_handle = tp_handle_lookup (room_repo, room, NULL, NULL);
  g_free (room);

  /* the message is nothing to do with MUC, do nothing */
  if (room_handle == 0)
    {
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  /* find the MUC channel */
  chan = g_hash_table_lookup (priv->text_channels,
      GUINT_TO_POINTER (room_handle));

  if (chan == NULL)
    {
      NODE_DEBUG (message->node, "ignoring MUC message from known "
          "handle with no corresponding channel");

      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  /* get the handle of the sender, which is either the room
   * itself or one of its members */
  if (strchr (from, '/') == NULL)
    {
      handle_source = room_repo;
      handle_type = TP_HANDLE_TYPE_ROOM;
      handle = room_handle;
      tp_handle_ref (room_repo, handle);
    }
  else
    {
      handle_source = contact_repo;
      handle_type = TP_HANDLE_TYPE_CONTACT;
      handle = tp_handle_ensure (contact_repo, from,
          GUINT_TO_POINTER (GABBLE_JID_ROOM_MEMBER), NULL);

      if (handle == 0)
        {
          NODE_DEBUG (message->node, "MUC message from invalid JID; ignoring");
          return LM_HANDLER_RESULT_REMOVE_MESSAGE;
        }

      /* anything other than a type="groupchat" is from the person directly and
       * simply relayed by the MUC, so should be left to the normal handlers */
      if (lm_message_get_sub_type (message) != LM_MESSAGE_SUB_TYPE_GROUPCHAT)
        {
          tp_handle_unref (contact_repo, handle);

          return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
        }
    }

  if (send_error != GABBLE_TEXT_CHANNEL_SEND_NO_ERROR)
    {
      tp_svc_channel_type_text_emit_send_error ((TpSvcChannelTypeText *) chan,
          send_error, stamp, msgtype, body);
      goto done;
    }

  if (state != -1 && handle_type == TP_HANDLE_TYPE_CONTACT)
    _gabble_muc_channel_state_receive (chan, state, handle);

  if (body != NULL)
    _gabble_muc_channel_receive (chan, msgtype, handle_type, handle, stamp,
        body, message);

  subj_node = lm_message_node_get_child (message->node, "subject");
  if (subj_node != NULL)
    {
      subject = lm_message_node_get_value (subj_node);
      _gabble_muc_channel_handle_subject (chan, msgtype, handle_type, handle,
          stamp, subject, message);
    }

done:
  tp_handle_unref (handle_source, handle);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}


/**
 * connection_presence_muc_cb:
 * @handler: #LmMessageHandler for this message
 * @connection: #LmConnection that originated the message
 * @message: the presence message
 * @user_data: callback data
 *
 * Called by loudmouth when we get an incoming <presence>.
 */
static LmHandlerResult
muc_factory_presence_cb (LmMessageHandler *handler,
                            LmConnection *lmconn,
                            LmMessage *msg,
                            gpointer user_data)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (user_data);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_ROOM);
  const char *from;
  LmMessageSubType sub_type;
  GabbleMucChannel *muc_chan = NULL;
  LmMessageNode *x_node;
  TpHandle room_handle;

  g_assert (lmconn == priv->conn->lmconn);

  from = lm_message_node_get_attribute (msg->node, "from");

  if (from == NULL)
    {
      NODE_DEBUG (msg->node,
          "presence stanza without from attribute, ignoring");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  sub_type = lm_message_get_sub_type (msg);

  room_handle = gabble_get_room_handle_from_jid (room_repo, from);
  if (room_handle != 0)
    muc_chan = g_hash_table_lookup (priv->text_channels,
        GUINT_TO_POINTER (room_handle));

  /* is it an error and for a MUC? */
  if (sub_type == LM_MESSAGE_SUB_TYPE_ERROR
      && muc_chan != NULL)
    {
      _gabble_muc_channel_presence_error (muc_chan, from, msg->node);

      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  x_node = lm_message_node_get_child_with_namespace (msg->node, "x",
      NS_MUC_USER);

  /* is it a MUC member presence? */
  if (x_node != NULL)
    {
      if (muc_chan != NULL)
        {
          TpHandle handle;

          handle = tp_handle_ensure (contact_repo, from,
              GUINT_TO_POINTER (GABBLE_JID_ROOM_MEMBER), NULL);
          if (handle == 0)
            {
              NODE_DEBUG (msg->node,
                  "discarding MUC presence from malformed jid");
              return LM_HANDLER_RESULT_REMOVE_MESSAGE;
            }

          _gabble_muc_channel_member_presence_updated (muc_chan, handle,
                                                       msg, x_node);
          tp_handle_unref (contact_repo, handle);
        }
      else
        {
          NODE_DEBUG (msg->node, "discarding unexpected MUC member presence");

          return LM_HANDLER_RESULT_REMOVE_MESSAGE;
        }
    }

  /* is it presence from an in-MUC JID containing tubes info? */

  if (muc_chan != NULL)
    {
      TpHandle handle;
      GabbleTubesChannel *tubes_chan;

      tubes_chan = g_hash_table_lookup (priv->tubes_channels,
          GINT_TO_POINTER (room_handle));
      if (tubes_chan == NULL)
        {
          LmMessageNode *tubes_node;

          tubes_node = lm_message_node_get_child_with_namespace (
              msg->node, "tubes", NS_TUBES);

          if (tubes_node == NULL)
            /* presence doesn't contain tubes information, no need
             * to create a tubes channel */
            return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

          tubes_chan = new_tubes_channel (fac, room_handle, muc_chan);
          tp_channel_factory_iface_emit_new_channel (fac,
              (TpChannelIface *) tubes_chan, NULL);
        }

      handle = tp_handle_ensure (contact_repo, from,
          GUINT_TO_POINTER (GABBLE_JID_ROOM_MEMBER), NULL);
      if (handle == 0)
        {
          NODE_DEBUG (msg->node,
              "discarding Tubes presence from malformed jid");
          return LM_HANDLER_RESULT_REMOVE_MESSAGE;
        }

      gabble_tubes_channel_presence_updated (tubes_chan, handle,
          msg);
      tp_handle_unref (contact_repo, handle);
    }

  return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

static void
roomlist_channel_closed_cb (GabbleRoomlistChannel *chan, gpointer data)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (data);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);

  if (priv->roomlist_channel != NULL)
    {
      g_object_unref (priv->roomlist_channel);
      priv->roomlist_channel = NULL;
    }
}

static gboolean
make_roomlist_channel (GabbleMucFactory *fac, gpointer request)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  TpBaseConnection *conn = (TpBaseConnection *) priv->conn;

  const gchar *server;
  gchar *object_path;

  g_assert (priv->roomlist_channel == NULL);

  server = _gabble_connection_find_conference_server (priv->conn);

  if (server == NULL)
    return FALSE;

  object_path = g_strdup_printf ("%s/RoomlistChannel",
      conn->object_path);

  priv->roomlist_channel = _gabble_roomlist_channel_new (priv->conn,
      object_path, server);

  g_signal_connect (priv->roomlist_channel, "closed",
                    (GCallback) roomlist_channel_closed_cb, fac);

  tp_channel_factory_iface_emit_new_channel (fac,
      (TpChannelIface *) priv->roomlist_channel, request);

  g_free (object_path);

  return TRUE;
}



static void
gabble_muc_factory_iface_close_all (TpChannelFactoryIface *iface)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (iface);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);

  DEBUG ("closing channels");

  if (priv->text_needed_for_tubes != NULL)
    {
      g_hash_table_destroy (priv->text_needed_for_tubes);
      priv->text_needed_for_tubes = NULL;
    }

  if (priv->text_channels != NULL)
    {
      GHashTable *tmp = priv->text_channels;
      priv->text_channels = NULL;
      g_hash_table_destroy (tmp);
    }

  if (priv->tubes_channels != NULL)
    {
      GHashTable *tmp = priv->tubes_channels;
      priv->tubes_channels = NULL;
      g_hash_table_destroy (tmp);
    }

  if (priv->roomlist_channel != NULL)
    {
      GObject *tmp = G_OBJECT (priv->roomlist_channel);
      priv->roomlist_channel = NULL;
      g_object_unref (tmp);
    }
}

static void
gabble_muc_factory_iface_connecting (TpChannelFactoryIface *iface)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (iface);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);

  DEBUG ("adding callbacks");

  g_assert (priv->message_cb == NULL);
  g_assert (priv->presence_cb == NULL);

  priv->message_cb = lm_message_handler_new (muc_factory_message_cb, fac,
      NULL);
  lm_connection_register_message_handler (priv->conn->lmconn, priv->message_cb,
                                          LM_MESSAGE_TYPE_MESSAGE,
                                          LM_HANDLER_PRIORITY_NORMAL);

  priv->presence_cb = lm_message_handler_new (muc_factory_presence_cb,
      fac, NULL);
  lm_connection_register_message_handler (priv->conn->lmconn,
      priv->presence_cb, LM_MESSAGE_TYPE_PRESENCE, LM_HANDLER_PRIORITY_NORMAL);
}


static void
gabble_muc_factory_iface_connected (TpChannelFactoryIface *iface)
{
  /* nothing to do */
}

static void
gabble_muc_factory_iface_disconnected (TpChannelFactoryIface *iface)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (iface);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);

  DEBUG ("removing callbacks");

  g_assert (priv->message_cb != NULL);
  g_assert (priv->presence_cb != NULL);

  lm_connection_unregister_message_handler (priv->conn->lmconn,
      priv->message_cb, LM_MESSAGE_TYPE_MESSAGE);
  lm_message_handler_unref (priv->message_cb);
  priv->message_cb = NULL;

  lm_connection_unregister_message_handler (priv->conn->lmconn,
      priv->presence_cb, LM_MESSAGE_TYPE_PRESENCE);
  lm_message_handler_unref (priv->presence_cb);
  priv->presence_cb = NULL;
}

struct _ForeachData
{
  TpChannelFunc foreach;
  gpointer user_data;
};

static void
_foreach_slave (gpointer key, gpointer value, gpointer user_data)
{
  struct _ForeachData *data = (struct _ForeachData *) user_data;
  TpChannelIface *chan = TP_CHANNEL_IFACE (value);

  data->foreach (chan, data->user_data);
}

static void
gabble_muc_factory_iface_foreach (TpChannelFactoryIface *iface,
                                  TpChannelFunc foreach,
                                  gpointer user_data)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (iface);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  struct _ForeachData data;

  data.user_data = user_data;
  data.foreach = foreach;

  g_hash_table_foreach (priv->text_channels, _foreach_slave, &data);
  g_hash_table_foreach (priv->tubes_channels, _foreach_slave, &data);

  if (priv->roomlist_channel != NULL)
    foreach (TP_CHANNEL_IFACE (priv->roomlist_channel), user_data);
}

/**
 * ensure_muc_channel:
 *
 * Create a MUC channel in response to RequestChannel.
 *
 * Return TRUE if it already existed, or return FALSE
 * if it needed to be created (so isn't ready yet).
 */
static gboolean
ensure_muc_channel (GabbleMucFactory *fac,
                    GabbleMucFactoryPrivate *priv,
                    TpHandle handle,
                    GabbleMucChannel **ret)
{
  TpBaseConnection *base_conn = (TpBaseConnection *) priv->conn;

  *ret = g_hash_table_lookup (priv->text_channels, GINT_TO_POINTER (handle));

  if (*ret == NULL)
    {
      *ret = new_muc_channel (fac, handle, TRUE, base_conn->self_handle, NULL);
      return FALSE;
    }

  if (_gabble_muc_channel_is_ready (*ret))
    return TRUE;
  else
    return FALSE;
}

static TpChannelFactoryRequestStatus
gabble_muc_factory_iface_request (TpChannelFactoryIface *iface,
                                  const gchar *chan_type,
                                  TpHandleType handle_type,
                                  guint handle,
                                  gpointer request,
                                  TpChannelIface **ret,
                                  GError **error)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (iface);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_ROOM);
  GabbleMucChannel *text_chan;
  GabbleTubesChannel *tubes_chan;

  if (!tp_strdiff (chan_type, TP_IFACE_CHANNEL_TYPE_ROOM_LIST))
    {
      if (priv->roomlist_channel != NULL)
        {
          *ret = TP_CHANNEL_IFACE (priv->roomlist_channel);
          return TP_CHANNEL_FACTORY_REQUEST_STATUS_EXISTING;
        }

      /* FIXME - delay if services aren't discovered yet? */
      if (!make_roomlist_channel (fac, request))
        {
          DEBUG ("no conference server available for roomlist request");
          return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;
        }
      *ret = TP_CHANNEL_IFACE (priv->roomlist_channel);
      return TP_CHANNEL_FACTORY_REQUEST_STATUS_CREATED;
    }

  if (handle_type != TP_HANDLE_TYPE_ROOM)
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;

  if (!tp_handle_is_valid (room_repo, handle, NULL))
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE;

  if (!tp_strdiff (chan_type, TP_IFACE_CHANNEL_TYPE_TEXT))
    {
      if (ensure_muc_channel (fac, priv, handle, &text_chan))
        {
          *ret = TP_CHANNEL_IFACE (text_chan);
          return TP_CHANNEL_FACTORY_REQUEST_STATUS_EXISTING;
        }
      else
        {
          return TP_CHANNEL_FACTORY_REQUEST_STATUS_QUEUED;
        }
    }
  else if (!tp_strdiff (chan_type, TP_IFACE_CHANNEL_TYPE_TUBES))
    {
      tubes_chan = g_hash_table_lookup (priv->tubes_channels,
          GINT_TO_POINTER (handle));
      if (tubes_chan != NULL)
        {
          *ret = TP_CHANNEL_IFACE (tubes_chan);
          return TP_CHANNEL_FACTORY_REQUEST_STATUS_EXISTING;
        }
      else
        {
          if (ensure_muc_channel (fac, priv, handle, &text_chan))
            {
              tubes_chan = new_tubes_channel (fac, handle, text_chan);
              *ret = TP_CHANNEL_IFACE (tubes_chan);
              tp_channel_factory_iface_emit_new_channel (fac, *ret, NULL);
              return TP_CHANNEL_FACTORY_REQUEST_STATUS_CREATED;
            }
          else
            {
              tubes_chan = new_tubes_channel (fac, handle, text_chan);
              g_hash_table_insert (priv->text_needed_for_tubes,
                  text_chan, tubes_chan);
              return TP_CHANNEL_FACTORY_REQUEST_STATUS_QUEUED;
            }
        }
    }
  else
    {
      return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;
    }
}

void
gabble_muc_factory_handle_si_stream_request (GabbleMucFactory *self,
                                             GabbleBytestreamIface *bytestream,
                                             TpHandle room_handle,
                                             const gchar *stream_id,
                                             LmMessage *msg)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (self);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
     (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_ROOM);
  GabbleTubesChannel *chan;

  g_return_if_fail (tp_handle_is_valid (room_repo, room_handle, NULL));

  chan = g_hash_table_lookup (priv->tubes_channels,
      GUINT_TO_POINTER (room_handle));
  if (chan == NULL)
    {
      GError e = { GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
          "No tubes channel available for this MUC" };

      DEBUG ("tubes channel doesn't exist for muc %d", room_handle);
      gabble_bytestream_iface_close (bytestream, &e);
      return;
    }

  gabble_tubes_channel_bytestream_offered (chan, bytestream, msg);
}

GabbleMucChannel *
gabble_muc_factory_find_text_channel (GabbleMucFactory *self,
                                      TpHandle handle)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (self);

  return g_hash_table_lookup (priv->text_channels, GUINT_TO_POINTER (handle));
}

static void
gabble_muc_factory_iface_init (gpointer g_iface,
                              gpointer iface_data)
{
  TpChannelFactoryIfaceClass *klass = (TpChannelFactoryIfaceClass *) g_iface;

  klass->close_all = gabble_muc_factory_iface_close_all;
  klass->connecting = gabble_muc_factory_iface_connecting;
  klass->connected = gabble_muc_factory_iface_connected;
  klass->disconnected = gabble_muc_factory_iface_disconnected;
  klass->foreach = gabble_muc_factory_iface_foreach;
  klass->request = gabble_muc_factory_iface_request;
}
