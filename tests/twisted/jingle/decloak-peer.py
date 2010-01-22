"""
Test use-case when client attempts to call an unsubscribed contact. Gabble
should ask them to "de-cloak".
"""

from gabbletest import exec_test
from servicetest import make_channel_proxy, call_async, sync_dbus
import jingletest

import dbus

import constants as cs
import ns

def test(q, bus, conn, stream):
    jt = jingletest.JingleTest(stream, 'test@localhost', 'foo@bar.com/Foo')
    jt2 = jingletest.JingleTest(stream, 'test@localhost', 'foo2@bar.com/Foo')
    # Make gabble think this is a different client
    jt2.remote_caps['node'] = 'http://example.com/fake-client1'

    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])

    q.expect('stream-authenticated')
    q.expect('dbus-signal', signal='PresenceUpdate',
        args=[{1L: (0L, {u'available': {}})}])
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    run_test(q, bus, conn, stream, jt, True)
    run_test(q, bus, conn, stream, jt2, False)

def run_test(q, bus, conn, stream, jt, decloak_allowed):
    """
    Requests streams on a media channel to jt.remote_jid without having their
    presence at all.
    """

    request = dbus.Dictionary({ cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAMED_MEDIA,
                                cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
                                cs.TARGET_ID: jt.remote_jid
                              }, signature='sv')
    path, props = conn.CreateChannel(request, dbus_interface=cs.CONN_IFACE_REQUESTS)
    media_iface = make_channel_proxy(conn, path, 'Channel.Type.StreamedMedia')
    handle = props[cs.TARGET_HANDLE]

    call_async(q, media_iface, 'RequestStreams', handle,
        [cs.MEDIA_STREAM_TYPE_AUDIO])

    q.expect('stream-presence') # the decloak

    if decloak_allowed:
        jt.send_remote_presence()
        info_event = q.expect('stream-iq', query_ns=ns.DISCO_INFO,
                to=jt.remote_jid)

        jt.send_remote_disco_reply(info_event.stanza)

        # RequestStreams should now happily complete
        q.expect('dbus-return', method='RequestStreams')
    else:
        q.expect('dbus-error', method='RequestStreams')

if __name__ == '__main__':
    exec_test(test, timeout=10)
