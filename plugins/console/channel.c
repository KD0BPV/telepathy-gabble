/* XML console plugin
 *
 * Copyright © 2011 Collabora Ltd. <http://www.collabora.co.uk/>
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
#include "console/channel.h"

#include <string.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include <wocky/wocky.h>
#include <gabble/gabble.h>
#include "extensions/extensions.h"

#include "console/debug.h"

enum {
    PROP_0,
    PROP_SPEW
};

struct _GabbleConsoleChannelPrivate
{
  WockySession *session;
  WockyXmppReader *reader;
  WockyXmppWriter *writer;

  /* %TRUE if we should emit signals when sending or receiving stanzas */
  gboolean spew;
  /* 0 if spew is FALSE; or a WockyPorter handler id for all incoming stanzas
   * if spew is TRUE. */
  guint incoming_handler;
  /* 0 if spew is FALSE; a GLib signal handler id for WockyPorter::sending if
   * spew is TRUE.
   */
  gulong sending_id;
};

static void console_iface_init (
    gpointer g_iface,
    gpointer data);
static void gabble_console_channel_set_spew (
    GabbleConsoleChannel *self,
    gboolean spew);
gchar *gabble_console_channel_get_path (TpBaseChannel *chan);
static void gabble_console_channel_close (TpBaseChannel *chan);

G_DEFINE_TYPE_WITH_CODE (GabbleConsoleChannel, gabble_console_channel,
    TP_TYPE_BASE_CHANNEL,
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_GABBLE_PLUGIN_CONSOLE,
      console_iface_init);
    )

static void
gabble_console_channel_init (GabbleConsoleChannel *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, GABBLE_TYPE_CONSOLE_CHANNEL,
      GabbleConsoleChannelPrivate);
  self->priv->reader = wocky_xmpp_reader_new_no_stream_ns (
      WOCKY_XMPP_NS_JABBER_CLIENT);
  self->priv->writer = wocky_xmpp_writer_new_no_stream ();
}


static void
gabble_console_channel_constructed (GObject *object)
{
  GabbleConsoleChannel *self = GABBLE_CONSOLE_CHANNEL (object);
  void (*chain_up)(GObject *) =
      G_OBJECT_CLASS (gabble_console_channel_parent_class)->constructed;

  if (chain_up != NULL)
    chain_up (object);

  self->priv->session = g_object_ref (
      gabble_plugin_connection_get_session (
          GABBLE_PLUGIN_CONNECTION (
              tp_base_channel_get_connection (
                  TP_BASE_CHANNEL (self)))));
  g_return_if_fail (self->priv->session != NULL);
}

static void
gabble_console_channel_get_property (
    GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  GabbleConsoleChannel *self = GABBLE_CONSOLE_CHANNEL (object);

  switch (property_id)
    {
      case PROP_SPEW:
        g_value_set_boolean (value, self->priv->spew);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    }
}

static void
gabble_console_channel_set_property (
    GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  GabbleConsoleChannel *self = GABBLE_CONSOLE_CHANNEL (object);

  switch (property_id)
    {
      case PROP_SPEW:
        gabble_console_channel_set_spew (self, g_value_get_boolean (value));
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    }
}

static void
gabble_console_channel_dispose (GObject *object)
{
  void (*chain_up) (GObject *) =
    G_OBJECT_CLASS (gabble_console_channel_parent_class)->dispose;
  GabbleConsoleChannel *self = GABBLE_CONSOLE_CHANNEL (object);

  gabble_console_channel_set_spew (self, FALSE);

  tp_clear_object (&self->priv->reader);
  tp_clear_object (&self->priv->writer);
  tp_clear_object (&self->priv->session);

  if (chain_up != NULL)
    chain_up (object);
}

static void
gabble_console_channel_class_init (GabbleConsoleChannelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  TpBaseChannelClass *channel_class = TP_BASE_CHANNEL_CLASS (klass);
  static TpDBusPropertiesMixinPropImpl console_props[] = {
      { "SpewStanzas", "spew-stanzas", "spew-stanzas" },
      { NULL },
  };

  object_class->constructed = gabble_console_channel_constructed;
  object_class->get_property = gabble_console_channel_get_property;
  object_class->set_property = gabble_console_channel_set_property;
  object_class->dispose = gabble_console_channel_dispose;

  channel_class->channel_type = GABBLE_IFACE_GABBLE_PLUGIN_CONSOLE;
  channel_class->get_object_path_suffix = gabble_console_channel_get_path;
  channel_class->close = gabble_console_channel_close;

  g_type_class_add_private (klass, sizeof (GabbleConsoleChannelPrivate));

  g_object_class_install_property (object_class, PROP_SPEW,
      g_param_spec_boolean ("spew-stanzas", "SpewStanzas",
          "If %TRUE, someone wants us to spit out a tonne of stanzas",
          FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  tp_dbus_properties_mixin_implement_interface (object_class,
      GABBLE_IFACE_QUARK_GABBLE_PLUGIN_CONSOLE,
      tp_dbus_properties_mixin_getter_gobject_properties,
      tp_dbus_properties_mixin_setter_gobject_properties,
      console_props);
}

gchar *
gabble_console_channel_get_path (TpBaseChannel *chan)
{
  return g_strdup_printf ("console%p", chan);
}

static void
gabble_console_channel_close (TpBaseChannel *chan)
{
  GabbleConsoleChannel *self = GABBLE_CONSOLE_CHANNEL (chan);

  gabble_console_channel_set_spew (self, FALSE);
  tp_base_channel_destroyed (chan);
}

static gboolean
incoming_cb (
    WockyPorter *porter,
    WockyStanza *stanza,
    gpointer user_data)
{
  GabbleConsoleChannel *self = GABBLE_CONSOLE_CHANNEL (user_data);
  const guint8 *body;
  gsize length;

  wocky_xmpp_writer_write_stanza (self->priv->writer, stanza, &body, &length);
  gabble_svc_gabble_plugin_console_emit_stanza_received (self,
      (const gchar *) body);
  return FALSE;
}

static void
sending_cb (
    WockyPorter *porter,
    WockyStanza *stanza,
    gpointer user_data)
{
  GabbleConsoleChannel *self = GABBLE_CONSOLE_CHANNEL (user_data);

  if (stanza != NULL)
    {
      const guint8 *body;
      gsize length;

      wocky_xmpp_writer_write_stanza (self->priv->writer, stanza, &body,
          &length);
      gabble_svc_gabble_plugin_console_emit_stanza_sent (self,
          (const gchar *) body);
    }
}

static void
gabble_console_channel_set_spew (
    GabbleConsoleChannel *self,
    gboolean spew)
{
  GabbleConsoleChannelPrivate *priv = self->priv;

  if (!spew != !priv->spew)
    {
      WockyPorter *porter = wocky_session_get_porter (self->priv->session);
      const gchar *props[] = { "SpewStanzas", NULL };

      priv->spew = spew;
      tp_dbus_properties_mixin_emit_properties_changed (G_OBJECT (self),
          GABBLE_IFACE_GABBLE_PLUGIN_CONSOLE, props);

      if (spew)
        {
          g_return_if_fail (priv->incoming_handler == 0);
          priv->incoming_handler = wocky_porter_register_handler_from_anyone (
              porter, WOCKY_STANZA_TYPE_NONE, WOCKY_STANZA_SUB_TYPE_NONE,
              WOCKY_PORTER_HANDLER_PRIORITY_MAX, incoming_cb, self, NULL);

          g_return_if_fail (priv->sending_id == 0);
          priv->sending_id = g_signal_connect (porter, "sending",
              (GCallback) sending_cb, self);
        }
      else
        {
          g_return_if_fail (priv->incoming_handler != 0);
          wocky_porter_unregister_handler (porter, priv->incoming_handler);
          priv->incoming_handler = 0;

          g_return_if_fail (priv->sending_id != 0);
          g_signal_handler_disconnect (porter, priv->sending_id);
          priv->sending_id = 0;
        }
    }
}

static void
return_from_send_iq (
    GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  GabbleConsoleChannel *self = GABBLE_CONSOLE_CHANNEL (source);
  GDBusMethodInvocation *context = user_data;
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (result);
  GError *error = NULL;

  if (g_simple_async_result_propagate_error (simple, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
  else
    {
      WockyStanza *reply = g_simple_async_result_get_op_res_gpointer (simple);
      WockyStanzaSubType sub_type;
      const guint8 *body;
      gsize length;

      wocky_stanza_get_type_info (reply, NULL, &sub_type);
      wocky_xmpp_writer_write_stanza (self->priv->writer, reply, &body, &length);

      /* woop woop */
      gabble_svc_gabble_plugin_console_return_from_send_iq (context,
          sub_type == WOCKY_STANZA_SUB_TYPE_RESULT ? "result" : "error",
          (const gchar *) body);
    }
}

static void
console_iq_reply_cb (
    GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  WockyPorter *porter = WOCKY_PORTER (source);
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (user_data);
  GError *error = NULL;
  WockyStanza *reply = wocky_porter_send_iq_finish (porter, result, &error);

  if (reply != NULL)
    {
      g_simple_async_result_set_op_res_gpointer (simple, reply, g_object_unref);
    }
  else
    {
      g_simple_async_result_set_from_error (simple, error);
      g_error_free (error);
    }

  g_simple_async_result_complete (simple);
  g_object_unref (simple);
}

static gboolean
get_iq_type (const gchar *type_str,
    WockyStanzaSubType *sub_type_out,
    GError **error)
{
  if (!wocky_strdiff (type_str, "get"))
    {
      *sub_type_out = WOCKY_STANZA_SUB_TYPE_GET;
      return TRUE;
    }

  if (!wocky_strdiff (type_str, "set"))
    {
      *sub_type_out = WOCKY_STANZA_SUB_TYPE_SET;
      return TRUE;
    }

  g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
      "Type must be 'get' or 'set', not '%s'", type_str);
  return FALSE;
}

static gboolean
validate_jid (const gchar **to,
    GError **error)
{
  if (tp_str_empty (*to))
    {
      *to = NULL;
      return TRUE;
    }

  if (wocky_decode_jid (*to, NULL, NULL, NULL))
    return TRUE;

  g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
      "'%s' is not a valid (or empty) JID", *to);
  return FALSE;
}

/*
 * @xml: doesn't actually have to be a top-level stanza. It can be the body of
 *  an IQ or whatever. If it has no namespace, it's assumed to be in
 *  jabber:client.
 */
static gboolean
parse_me_a_stanza (
    GabbleConsoleChannel *self,
    const gchar *xml,
    WockyStanza **stanza_out,
    GError **error)
{
  GabbleConsoleChannelPrivate *priv = self->priv;
  WockyStanza *stanza;

  wocky_xmpp_reader_reset (priv->reader);
  wocky_xmpp_reader_push (priv->reader, (const guint8 *) xml, strlen (xml));

  *error = wocky_xmpp_reader_get_error (priv->reader);

  if (*error != NULL)
    return FALSE;

  stanza = wocky_xmpp_reader_pop_stanza (priv->reader);

  if (stanza == NULL)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "Incomplete stanza! Bad person.");
      return FALSE;
    }

  *stanza_out = stanza;
  return TRUE;
}

static void
console_send_iq (
    GabbleSvcGabblePluginConsole *channel,
    const gchar *type_str,
    const gchar *to,
    const gchar *body,
    GDBusMethodInvocation *context)
{
  GabbleConsoleChannel *self = GABBLE_CONSOLE_CHANNEL (channel);
  WockyPorter *porter = wocky_session_get_porter (self->priv->session);
  WockyStanzaSubType sub_type;
  WockyStanza *fragment;
  GError *error = NULL;

  if (get_iq_type (type_str, &sub_type, &error) &&
      validate_jid (&to, &error) &&
      parse_me_a_stanza (self, body, &fragment, &error))
    {
      GSimpleAsyncResult *simple = g_simple_async_result_new (G_OBJECT (self),
          return_from_send_iq, context, console_send_iq);
      WockyStanza *stanza = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ, sub_type,
          NULL, to, NULL);

      wocky_node_add_node_tree (wocky_stanza_get_top_node (stanza),
          WOCKY_NODE_TREE (fragment));
      wocky_porter_send_iq_async (porter, stanza, NULL, console_iq_reply_cb, simple);
      g_object_unref (fragment);
    }
  else
    {
      DEBUG ("%s", error->message);
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}

static void
console_stanza_sent_cb (
    GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  WockyPorter *porter = WOCKY_PORTER (source);
  GDBusMethodInvocation *context = user_data;
  GError *error = NULL;

  if (wocky_porter_send_finish (porter, result, &error))
    {
      gabble_svc_gabble_plugin_console_return_from_send_stanza (context);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_clear_error (&error);
    }
}

static gboolean
stanza_looks_coherent (
    WockyStanza *stanza,
    GError **error)
{
  WockyNode *top_node = wocky_stanza_get_top_node (stanza);
  WockyStanzaType t;
  WockyStanzaSubType st;

  wocky_stanza_get_type_info (stanza, &t, &st);

  if (t == WOCKY_STANZA_TYPE_UNKNOWN)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "I don't know what a <%s xmlns='%s'/> is", top_node->name,
          g_quark_to_string (top_node->ns));
      return FALSE;
    }
  else if (st == WOCKY_STANZA_SUB_TYPE_UNKNOWN)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "I don't know what type='%s' means",
          wocky_node_get_attribute (top_node, "type"));
      return FALSE;
    }

  return TRUE;
}

static void
console_send_stanza (
    GabbleSvcGabblePluginConsole *channel,
    const gchar *xml,
    GDBusMethodInvocation *context)
{
  GabbleConsoleChannel *self = GABBLE_CONSOLE_CHANNEL (channel);
  WockyPorter *porter = wocky_session_get_porter (self->priv->session);
  WockyStanza *stanza = NULL;
  GError *error = NULL;

  if (parse_me_a_stanza (self, xml, &stanza, &error) &&
      stanza_looks_coherent (stanza, &error))
    {
      wocky_porter_send_async (porter, stanza, NULL, console_stanza_sent_cb,
          context);
    }
  else
    {
      DEBUG ("%s", error->message);
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }

  tp_clear_object (&stanza);
}

static void
console_iface_init (
    gpointer klass,
    gpointer data G_GNUC_UNUSED)
{
#define IMPLEMENT(x) gabble_svc_gabble_plugin_console_implement_##x (\
    klass, console_##x)
  IMPLEMENT (send_iq);
  IMPLEMENT (send_stanza);
#undef IMPLEMENT
}
