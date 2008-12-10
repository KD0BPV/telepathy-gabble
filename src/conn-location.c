
#include "config.h"
#include "conn-location.h"
#include "presence-cache.h"

#include <stdlib.h>

#define DEBUG_FLAG GABBLE_DEBUG_LOCATION

#include "debug.h"
#include "extensions/extensions.h"
#include "namespaces.h"
#include "pubsub.h"

#define XEP0080_ALT "alt"
#define XEP0080_AREA "area"
#define XEP0080_BEARING "bearing"
#define XEP0080_BUILDING "building"
#define XEP0080_COUNTRY "country"
#define XEP0080_DESCRIPTION "description"
#define XEP0080_ERROR "error"
#define XEP0080_FLOOR "floor"
#define XEP0080_LAT "lat"
#define XEP0080_LOCALITY "locality"
#define XEP0080_LON "lon"
#define XEP0080_POSTAL_CODE "postalcode"
#define XEP0080_REGION "region"
#define XEP0080_ROOM "room"
#define XEP0080_STREET "street"
#define XEP0080_TEXT "text"
#define XEP0080_TIMESTAMP "timestamp"
#define XEP0080_URI "uri"

#define LOCATION_ACCURACY_LEVEL "accuracy-level"
#define LOCATION_COUNTRY_CODE "countrycode"
#define LOCATION_VERTICAL_ERROR_M "vertical-error-m"
#define LOCATION_HORIZONTAL_ERROR_M "horizontal-error-m"

/* XXX: similar to conn-olpc.c's inspect_contact(), except that it assumes
 * that the handle is valid. (Does tp_handle_inspect check validity anyway?)
 * Reduce duplication.
 */
static const gchar *
inspect_contact (TpBaseConnection *base,
                 guint contact)
{
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      base, TP_HANDLE_TYPE_CONTACT);

  return tp_handle_inspect (contact_repo, contact);
}

static gboolean
validate_contacts (TpBaseConnection *base,
                   DBusGMethodInvocation *context,
                   const GArray *contacts)
{
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);
  GError *error = NULL;

  if (!tp_handles_are_valid (contact_handles, contacts, TRUE, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return FALSE;
    }

  return TRUE;
}

static gboolean
lm_message_node_get_double (LmMessageNode *node,
                            gdouble *d)
{
  const gchar *value;
  gchar *end;

  value = lm_message_node_get_value (node);

  if (value == NULL)
    return FALSE;

  *d = strtod (value, &end);

  if (end == value)
    return FALSE;

  return TRUE;
}
/*
static gboolean
lm_message_node_get_string (LmMessageNode *node,
                            gchar *s)
{
  const gchar *value;

  value = lm_message_node_get_value (node);

  if (value == NULL)
    return FALSE;

  s = g_strdup (value);
  return TRUE;
}
*/
static LmHandlerResult
pep_reply_cb (GabbleConnection *conn,
              LmMessage *sent_msg,
              LmMessage *reply_msg,
              GObject *object,
              gpointer user_data)
{
  TpBaseConnection *base = (TpBaseConnection *) conn;
  TpHandleRepoIface *contact_repo =
      tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);
  LmMessageNode *geoloc;
  LmMessageNode *lat_node;
  LmMessageNode *lon_node;
  GHashTable *result = NULL;
  const gchar *from;
  gdouble lat;
  gdouble lon;
  guint contact;

  result = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_free,
      (GDestroyNotify) tp_g_value_slice_free);
  from = lm_message_node_get_attribute (reply_msg->node, "from");

  if (from == NULL)
    goto END;

  contact = tp_handle_lookup (contact_repo, from, NULL, NULL);
  /* XXX: ref all the handles */
  g_assert (contact);
  geoloc = lm_message_node_find_child (reply_msg->node, "geoloc");

  if (geoloc == NULL)
    goto END;

  lat_node = lm_message_node_find_child (geoloc, "lat");
  lon_node = lm_message_node_find_child (geoloc, "lon");

  if (lat_node == NULL &&
      lon_node == NULL)
    goto END;

  if (lat_node != NULL && lm_message_node_get_double (lat_node, &lat))
    {
      GValue *value = g_slice_new0 (GValue);

      g_value_init (value, G_TYPE_DOUBLE);
      g_value_set_double (value, lat);
      g_hash_table_insert (result, g_strdup ("lat"), value);
    }

  if (lon_node != NULL && lm_message_node_get_double (lon_node, &lon))
    {
      GValue *value = g_slice_new0 (GValue);

      g_value_init (value, G_TYPE_DOUBLE);
      g_value_set_double (value, lon);
      g_hash_table_insert (result, g_strdup ("lon"), value);
    }

  DEBUG ("LocationsUpdate %s (%f, %f)", from, lat, lon);

  gabble_presence_cache_update_location (conn->presence_cache, contact,
      result);

  gabble_svc_connection_interface_location_emit_location_updated (conn,
      contact, result);

END:
  g_hash_table_destroy (result);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
location_get_locations (GabbleSvcConnectionInterfaceLocation *iface,
                        const GArray *contacts,
                        DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) conn;
  guint i;
  GHashTable *return_locations = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      NULL);
  GHashTable *location = NULL;

  DEBUG ("GetLocation for contacts:");

  if (!validate_contacts (base, context, contacts))
    return;

  for (i = 0; i < contacts->len; i++)
    {
      guint contact = g_array_index (contacts, guint, i);
      const gchar *jid = inspect_contact (base, contact);

      /* Check for cached locations */
      if (gabble_presence_cache_get_location (conn->presence_cache, contact, &location))
        {
          //FIXME: where to unref the location?
          DEBUG (" - %s: cached", jid);
          g_hash_table_insert (return_locations, GINT_TO_POINTER (contact), location);
        }
      else if (!pubsub_query (conn, jid, NS_GEOLOC, pep_reply_cb, NULL))
        {
          GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
              "Sending PEP location query failed" };

          dbus_g_method_return_error (context, &error);
          g_hash_table_unref (return_locations);

          return;
        } else
           DEBUG (" - %s: requested", jid);
    }

  gabble_svc_connection_interface_location_return_from_get_locations
      (context, return_locations);
  g_hash_table_unref (return_locations);

}

static void
location_set_location (GabbleSvcConnectionInterfaceLocation *iface,
                       GHashTable *location,
                       DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  LmMessage *msg;
  LmMessageNode *geoloc;
  GValue *lat_val;
  GValue *lon_val;

  msg = pubsub_make_publish_msg (NULL, NS_GEOLOC, NS_GEOLOC, "geoloc",
      &geoloc);

  DEBUG ("SetLocation to");

  lat_val = g_hash_table_lookup (location, "lat");
  lon_val = g_hash_table_lookup (location, "lon");

  if (lat_val && G_VALUE_TYPE (lat_val) == G_TYPE_DOUBLE)
    {
      gchar *lat_str;

      lat_str = g_strdup_printf ("%.6f", g_value_get_double (lat_val));
      lm_message_node_add_child (geoloc, "lat", lat_str);
      DEBUG ("\tLatitude: %s", lat_str);
      g_free (lat_str);
    }

  if (lon_val && G_VALUE_TYPE (lon_val) == G_TYPE_DOUBLE)
    {
      gchar *lon_str;

      lon_str = g_strdup_printf ("%.6f", g_value_get_double (lon_val));
      lm_message_node_add_child (geoloc, "lon", lon_str);
      DEBUG ("\tLongitude: %s", lon_str);
      g_free (lon_str);
    }

  /* XXX: use _ignore_reply */
  if (!_gabble_connection_send (conn, msg, NULL))
    {
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
          "Failed to send msg" };

      dbus_g_method_return_error (context, &error);
      return;
    }


  dbus_g_method_return (context);
}

void
location_iface_init (gpointer g_iface, gpointer iface_data)
{
  GabbleSvcConnectionInterfaceLocationClass *klass = g_iface;

#define IMPLEMENT(x) gabble_svc_connection_interface_location_implement_##x \
  (klass, location_##x)
  IMPLEMENT(get_locations);
  IMPLEMENT(set_location);
#undef IMPLEMENT
}

void
conn_location_propeties_getter (GObject *object,
                                GQuark interface,
                                GQuark name,
                                GValue *value,
                                gpointer getter_data)
{
  /* GabbleConnection *conn = GABBLE_CONNECTION (object); */

  if (!tp_strdiff (g_quark_to_string (name), "LocationAccessControlTypes"))
    {
      GabbleRichPresenceAccessControlType access_control_type =
        GABBLE_RICH_PRESENCE_ACCESS_CONTROL_TYPE_PUBLISH_LIST;
      GArray *access_control = g_array_sized_new (FALSE, FALSE,
          sizeof (GabbleRichPresenceAccessControlType), 1);

      g_array_append_val (access_control, access_control_type);
      g_value_take_boxed (value, access_control);
    }
  else if (!tp_strdiff (g_quark_to_string (name), "LocationAccessControl"))
    {
      GValueArray *access_control = g_value_array_new (2);
      GValue type = {0,};
      GValue variant = {0,};
      GValue *allocated_value;

      DEBUG ("%s", g_type_name (G_VALUE_TYPE (value)));

      /* G_TYPE_INT is the type of GabbleRichPresenceAccessControlType */
      g_value_init (&type, G_TYPE_INT);
      g_value_set_int (&type,
          GABBLE_RICH_PRESENCE_ACCESS_CONTROL_TYPE_PUBLISH_LIST);
      g_value_array_append (access_control, &type);
      g_value_unset (&type);

      g_value_init (&variant, G_TYPE_POINTER);
      /* G_TYPE_UINT is a random type, it is not used */
      allocated_value = tp_g_value_slice_new (G_TYPE_UINT);
      g_value_set_pointer (&variant, allocated_value);
      g_value_array_append (access_control, &variant);
      g_value_unset (&variant);

      g_value_take_boxed (value, access_control);
    }
  else
    {
      g_assert_not_reached ();
    }
}

