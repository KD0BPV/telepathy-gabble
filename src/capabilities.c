/*
 * capabilities.c - Connection.Interface.Capabilities constants and utilities
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

#include "capabilities.h"

#include "namespaces.h"
#include "config.h"
#include "presence-cache.h"
#include <telepathy-glib/interfaces.h>
#include "gabble-media-channel.h"

static const Feature self_advertised_features[] =
{
  { VERSION, NS_GOOGLE_FEAT_SESSION, 0},
  { VERSION, NS_GOOGLE_TRANSPORT_P2P, PRESENCE_CAP_GOOGLE_TRANSPORT_P2P},
  { VERSION, NS_JINGLE, PRESENCE_CAP_JINGLE},
  { VERSION, NS_CHAT_STATES, PRESENCE_CAP_CHAT_STATES},
  { VERSION, NS_NICK, 0},
  { VERSION, NS_NICK "+notify", 0},
  { VERSION, NS_SI, PRESENCE_CAP_SI},
  { VERSION, NS_IBB, PRESENCE_CAP_IBB},
  { VERSION, NS_TUBES, PRESENCE_CAP_SI_TUBES},

  { BUNDLE_VOICE_V1, NS_GOOGLE_FEAT_VOICE, PRESENCE_CAP_GOOGLE_VOICE},
  { BUNDLE_JINGLE_AUDIO, NS_JINGLE_DESCRIPTION_AUDIO,
    PRESENCE_CAP_JINGLE_DESCRIPTION_AUDIO},
  { BUNDLE_JINGLE_VIDEO, NS_JINGLE_DESCRIPTION_VIDEO,
    PRESENCE_CAP_JINGLE_DESCRIPTION_VIDEO},

  { BUNDLE_OLPC_1, NS_OLPC_BUDDY_PROPS "+notify", PRESENCE_CAP_OLPC_1},
  { BUNDLE_OLPC_1, NS_OLPC_ACTIVITIES "+notify", PRESENCE_CAP_OLPC_1},
  { BUNDLE_OLPC_1, NS_OLPC_CURRENT_ACTIVITY "+notify", PRESENCE_CAP_OLPC_1},
  { BUNDLE_OLPC_1, NS_OLPC_ACTIVITY_PROPS "+notify", PRESENCE_CAP_OLPC_1},

  { NULL, NULL, 0}
};

GSList *
capabilities_get_features (GabblePresenceCapabilities caps)
{
  GSList *features = NULL;
  const Feature *i;

  for (i = self_advertised_features; NULL != i->ns; i++)
    if ((i->caps & caps) == i->caps)
      features = g_slist_append (features, (gpointer) i);

  return features;
}

void
capabilities_fill_cache (GabblePresenceCache *cache)
{
  const Feature *feat;

  /* We don't advertise bundles anymore, but we keep them in the cache. So if
   * we speak to an old version of Gabble, we don't need to make discovery
   * requests for theses bundles. */
  for (feat = self_advertised_features; NULL != feat->ns; feat++)
    {
      gchar *node = g_strconcat (NS_GABBLE_CAPS "#", feat->bundle, NULL);
      gabble_presence_cache_add_bundle_caps (cache,
          node, feat->caps);
      g_free (node);
    }

  /* Cache this bundle from the Google Talk client as trusted. So Gabble will
   * not send any discovery request for this bundle.
   *
   * XMPP does not require to cache this bundle but some old versions of
   * Google Talk do not reply correctly to discovery requests. */
  gabble_presence_cache_add_bundle_caps (cache,
    "http://www.google.com/xmpp/client/caps#voice-v1",
    PRESENCE_CAP_GOOGLE_VOICE);
}

GabblePresenceCapabilities
capabilities_get_initial_caps ()
{
  GabblePresenceCapabilities ret = 0;
  const Feature *feat;

  for (feat = self_advertised_features; NULL != feat->ns; feat++)
    {
      if (g_str_equal (feat->bundle, VERSION))
        {
          /* VERSION == bundle means a fixed feature, which we always
           * advertise */
          ret |= feat->caps;
        }
    }

  return ret;
}

const CapabilityConversionData capabilities_conversions[] =
{
  { TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA,
    _gabble_media_channel_typeflags_to_caps,
    _gabble_media_channel_caps_to_typeflags },
  { NULL, NULL, NULL}
};

