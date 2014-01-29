"""
Test text channel initiated by me, using Requests.
"""

import dbus

from gabbletest import exec_test
from servicetest import (call_async, EventPattern, assertContains,
        assertEquals)
import constants as cs

def test(q, bus, conn, stream):
    self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")

    jid = 'foo@bar.com'
    foo_handle = conn.get_contact_handle_sync(jid)

    properties = conn.GetAll(
        cs.CONN_IFACE_REQUESTS, dbus_interface=dbus.PROPERTIES_IFACE)
    assert properties.get('Channels') == [], properties['Channels']

    properties = conn.GetAll(
        cs.CONN, dbus_interface=dbus.PROPERTIES_IFACE)
    assert ({cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
             cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
             },
             [cs.TARGET_HANDLE, cs.TARGET_ID],
             ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    call_async(q, conn.Requests, 'CreateChannel',
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              cs.TARGET_HANDLE: foo_handle,
              })

    ret, new_sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        )

    assert len(ret.value) == 2
    emitted_props = ret.value[1]
    assert emitted_props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_TEXT
    assert emitted_props[cs.TARGET_HANDLE_TYPE] == cs.HT_CONTACT
    assert emitted_props[cs.TARGET_HANDLE] == foo_handle
    assert emitted_props[cs.TARGET_ID] == jid
    assert emitted_props[cs.REQUESTED] == True
    assert emitted_props[cs.INITIATOR_HANDLE] == self_handle
    assert emitted_props[cs.INITIATOR_ID] == 'test@localhost'
    assertContains('text/plain', emitted_props[cs.SUPPORTED_CONTENT_TYPES])
    assertEquals(0, emitted_props[cs.MESSAGE_PART_SUPPORT_FLAGS])
    assertEquals(cs.DELIVERY_REPORTING_SUPPORT_FLAGS_RECEIVE_FAILURES,
            emitted_props[cs.DELIVERY_REPORTING_SUPPORT])

    assert new_sig.args[0] == ret.value[0]
    assert new_sig.args[1] == ret.value[1]

    properties = conn.GetAll(
        cs.CONN_IFACE_REQUESTS, dbus_interface=dbus.PROPERTIES_IFACE)

    assertContains((new_sig.args[0], new_sig.args[1]), properties['Channels'])

if __name__ == '__main__':
    exec_test(test)
