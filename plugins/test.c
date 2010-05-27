#include "test.h"

#include <stdio.h>

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/util.h>
#include <telepathy-glib/errors.h>

#include "extensions/extensions.h"

#include <gabble/plugin.h>

#define DEBUG(msg, ...) \
  g_debug ("%s: " msg, G_STRFUNC, ##__VA_ARGS__)

/*****************************
 * TestPlugin implementation *
 *****************************/

static void plugin_iface_init (
    gpointer g_iface,
    gpointer data);

#define IFACE_TEST "org.freedesktop.Telepathy.Gabble.Plugin.Test"
#define IFACE_TEST_PROPS IFACE_TEST ".Props"
#define IFACE_TEST_BUGGY IFACE_TEST ".Buggy"
#define IFACE_TEST_IQ IFACE_TEST ".IQ"

static const gchar * const sidecar_interfaces[] = {
    IFACE_TEST,
    IFACE_TEST_PROPS,
    IFACE_TEST_BUGGY,
    IFACE_TEST_IQ,
    NULL
};

G_DEFINE_TYPE_WITH_CODE (TestPlugin, test_plugin, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_PLUGIN, plugin_iface_init);
    )

static void
test_plugin_init (TestPlugin *object)
{
  DEBUG ("%p", object);
}

static void
test_plugin_class_init (TestPluginClass *klass)
{
}

static void
sidecar_iq_created_cb (
    GObject *source,
    GAsyncResult *new_result,
    gpointer user_data)
{
  GSimpleAsyncResult *result = user_data;
  GabbleSidecar *sidecar = GABBLE_SIDECAR (source);
  GError *error = NULL;

  if (g_async_initable_init_finish (G_ASYNC_INITABLE (source),
          new_result, &error))
    {
      g_simple_async_result_set_op_res_gpointer (result, sidecar,
          g_object_unref);
    }
  else
    {
      g_simple_async_result_set_from_error (result, error);
      g_clear_error (&error);
      g_object_unref (sidecar);
    }

  g_simple_async_result_complete (result);
  g_object_unref (result);
}

static void
test_plugin_create_sidecar (
    GabblePlugin *plugin,
    const gchar *sidecar_interface,
    TpBaseConnection *connection,
    WockySession *session,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *result = g_simple_async_result_new (G_OBJECT (plugin),
      callback, user_data,
      /* sic: all plugins share gabble_plugin_create_sidecar_finish() so we
       * need to use the same source tag.
       */
      gabble_plugin_create_sidecar);
  GabbleSidecar *sidecar = NULL;

  if (!tp_strdiff (sidecar_interface, IFACE_TEST))
    {
      sidecar = g_object_new (TEST_TYPE_SIDECAR, NULL);
    }
  else if (!tp_strdiff (sidecar_interface, IFACE_TEST_PROPS))
    {
      sidecar = g_object_new (TEST_TYPE_SIDECAR_PROPS, NULL);
    }
  else if (!tp_strdiff (sidecar_interface, IFACE_TEST_IQ))
    {
      g_async_initable_new_async (TEST_TYPE_SIDECAR_IQ, G_PRIORITY_DEFAULT,
          NULL, sidecar_iq_created_cb, result, "session", session, NULL);
      return;
    }
  else
    {
      /* This deliberately doesn't check for IFACE_TEST_BUGGY, to test Gabble's
       * reactions to buggy plugins. :)
       */
      g_simple_async_result_set_error (result, TP_ERRORS,
          TP_ERROR_NOT_IMPLEMENTED, "'%s' not implemented", sidecar_interface);
    }

  if (sidecar != NULL)
    g_simple_async_result_set_op_res_gpointer (result, sidecar, g_object_unref);

  g_simple_async_result_complete_in_idle (result);
  g_object_unref (result);
}

static void
plugin_iface_init (
    gpointer g_iface,
    gpointer data G_GNUC_UNUSED)
{
  GabblePluginInterface *iface = g_iface;

  iface->name = "Sidecar test plugin";
  iface->sidecar_interfaces = sidecar_interfaces;
  iface->create_sidecar = test_plugin_create_sidecar;
}

GabblePlugin *
gabble_plugin_create ()
{
  return g_object_new (test_plugin_get_type (), NULL);
}

/******************************
 * TestSidecar implementation *
 ******************************/

static void sidecar_iface_init (
    gpointer g_iface,
    gpointer data);

G_DEFINE_TYPE_WITH_CODE (TestSidecar, test_sidecar, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SIDECAR, sidecar_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_GABBLE_PLUGIN_TEST, NULL);
    )

static void
test_sidecar_init (TestSidecar *object)
{
  DEBUG ("%p", object);
}

static void
test_sidecar_class_init (TestSidecarClass *klass)
{
}

static void sidecar_iface_init (
    gpointer g_iface,
    gpointer data)
{
  GabbleSidecarInterface *iface = g_iface;

  iface->interface = IFACE_TEST;
  iface->get_immutable_properties = NULL;
}

/***********************************
 * TestSidecarProps implementation *
 ***********************************/

static void sidecar_props_iface_init (
    gpointer g_iface,
    gpointer data);

G_DEFINE_TYPE_WITH_CODE (TestSidecarProps, test_sidecar_props, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SIDECAR, sidecar_props_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_GABBLE_PLUGIN_TEST, NULL);
    )

static void
test_sidecar_props_init (TestSidecarProps *object)
{
  DEBUG ("%p", object);

  object->props = tp_asv_new (
      IFACE_TEST_PROPS ".Greeting", G_TYPE_STRING, "oh hai",
      NULL);
}

static void
test_sidecar_props_finalize (GObject *object)
{
  TestSidecarProps *self = TEST_SIDECAR_PROPS (object);
  void (*chain_up) (GObject *) =
      G_OBJECT_CLASS (test_sidecar_props_parent_class)->finalize;

  g_hash_table_unref (self->props);

  if (chain_up != NULL)
    chain_up (object);
}

static void
test_sidecar_props_class_init (TestSidecarPropsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = test_sidecar_props_finalize;
}

static GHashTable *
sidecar_props_get_immutable_properties (GabbleSidecar *sidecar)
{
  TestSidecarProps *self = TEST_SIDECAR_PROPS (sidecar);

  return g_hash_table_ref (self->props);
}

static void sidecar_props_iface_init (
    gpointer g_iface,
    gpointer data)
{
  GabbleSidecarInterface *iface = g_iface;

  iface->interface = IFACE_TEST_PROPS;
  iface->get_immutable_properties = sidecar_props_get_immutable_properties;
}

/********************************
 * TestSidecarIQ implementation *
 ********************************/

static void sidecar_iq_iface_init (
    gpointer g_iface,
    gpointer data);
static void async_initable_iface_init (
    gpointer g_iface,
    gpointer data);

G_DEFINE_TYPE_WITH_CODE (TestSidecarIQ, test_sidecar_iq, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SIDECAR, sidecar_iq_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_GABBLE_PLUGIN_TEST, NULL);
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, async_initable_iface_init);
    )

enum {
    PROP_SESSION = 1
};

static void
test_sidecar_iq_init (TestSidecarIQ *object)
{
  DEBUG ("%p", object);
}

static void
test_sidecar_iq_set_property (
    GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  TestSidecarIQ *self = TEST_SIDECAR_IQ (object);

  switch (property_id)
    {
      case PROP_SESSION:
        self->session = g_value_dup_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    }
}

static void
test_sidecar_iq_dispose (GObject *object)
{
  TestSidecarIQ *self = TEST_SIDECAR_IQ (object);

  DEBUG ("called for %p", object);

  if (self->session != NULL)
    {
      g_object_unref (self->session);
      self->session = NULL;
    }
}

static void
test_sidecar_iq_class_init (TestSidecarIQClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = test_sidecar_iq_set_property;
  object_class->dispose = test_sidecar_iq_dispose;

  g_object_class_install_property (object_class, PROP_SESSION,
      g_param_spec_object ("session", "SESSION",
          "THIS IS A WOCKY SESSION YOU CAN TELL BY THE TYPE",
          WOCKY_TYPE_SESSION,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

static void
sidecar_iq_iface_init (
    gpointer g_iface,
    gpointer data)
{
  GabbleSidecarInterface *iface = g_iface;

  iface->interface = IFACE_TEST_IQ;
  iface->get_immutable_properties = NULL;
}

static void
iq_cb (
    GObject *source,
    GAsyncResult *nested_result,
    gpointer user_data)
{
  GSimpleAsyncResult *result = user_data;
  GError *error = NULL;
  WockyStanza *reply;

  reply = wocky_porter_send_iq_finish (WOCKY_PORTER (source),
      nested_result, &error);

  if (reply == NULL)
    {
      g_simple_async_result_set_from_error (result, error);
      g_clear_error (&error);
    }
  else
    {
      WockyStanzaSubType t;

      wocky_stanza_get_type_info (reply, NULL, &t);

      if (t == WOCKY_STANZA_SUB_TYPE_RESULT)
        g_simple_async_result_set_op_res_gboolean (result, TRUE);
      else
        g_simple_async_result_set_error (result, TP_ERRORS,
            TP_ERROR_NOT_AVAILABLE, "server said no!");

      g_object_unref (reply);
    }

  g_simple_async_result_complete (result);
}

static void
sidecar_iq_init_async (
    GAsyncInitable *initable,
    int io_priority,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  TestSidecarIQ *self = TEST_SIDECAR_IQ (initable);
  GSimpleAsyncResult *result = g_simple_async_result_new (G_OBJECT (self),
      callback, user_data, sidecar_iq_init_async);
  WockyPorter *porter = wocky_session_get_porter (self->session);
  WockyStanza *iq;

  iq = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_GET, NULL, "sidecar.example.com",
        '(', "query",
          ':', "http://example.com/sidecar",
          '(', "oh-hai",
          ')',
        ')',
      NULL);
  wocky_porter_send_iq_async (porter, iq, cancellable, iq_cb, result);
}

static gboolean
sidecar_iq_init_finish (
    GAsyncInitable *initable,
    GAsyncResult *result,
    GError **error)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (result);

  if (g_simple_async_result_propagate_error (simple, error))
    return FALSE;

  g_return_val_if_fail (g_simple_async_result_is_valid (result,
      G_OBJECT (initable), sidecar_iq_init_async), FALSE);

  return g_simple_async_result_get_op_res_gboolean (simple);
}

static void
async_initable_iface_init (
    gpointer g_iface,
    gpointer data)
{
  GAsyncInitableIface *iface = g_iface;

  iface->init_async = sidecar_iq_init_async;
  iface->init_finish = sidecar_iq_init_finish;
}