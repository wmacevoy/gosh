# Forwarding: forwards and the stream, carried over the side channel (design v2, 2.2).
# gosh's own file. tools/build.sh copies this directory into its copy of the core, and
# seam/05-build.patch tells the core's build that it is there.
AM_CPPFLAGS = -I$(top_srcdir)/ $(protobuf_CFLAGS) $(CRYPTO_CFLAGS)
AM_CXXFLAGS = $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = libgoshforwarding.a

libgoshforwarding_a_SOURCES = forwarding.h udpforward.cc udpforward.h tcpforward.cc tcpforward.h tcpadapter.cc tcpadapter.h tcpinudp.cc tcpinudp.h connection.h

# The fork's tests of these classes, which call them directly. They hold this code
# to what it did at pf.7 while it is carried unchanged; they go when it is reshaped.
# connection-diff holds the reshaped connection logic to the fork's adapter, segment for
# segment, over scripted scenarios (queue item 15). It supplies the clock the old class reads.
# connection-test is the logic by itself, for what the differential test cannot reach.
check_PROGRAMS = udp-forward tcp-in-udp tcp-forward connection-diff connection-test
TESTS = $(check_PROGRAMS)
LDADD = libgoshforwarding.a ../network/libmoshnetwork.a ../crypto/libmoshcrypto.a ../util/libmoshutil.a $(CRYPTO_LIBS)
udp_forward_SOURCES = udp-forward.cc
tcp_in_udp_SOURCES = tcp-in-udp.cc
tcp_forward_SOURCES = tcp-forward.cc
connection_diff_SOURCES = connection-diff.cc
connection_diff_LDADD = libgosholdadapter.a $(LDADD)

# The fork's adapter as it was, under another name, for connection-diff alone: the programs
# no longer use it. It goes when forwarding has landed (queue item 15).
check_LIBRARIES = libgosholdadapter.a
libgosholdadapter_a_SOURCES = old-tcpadapter.cc old-tcpadapter.h
libgosholdadapter_a_CPPFLAGS = $(AM_CPPFLAGS) -DAdapterPort=OldAdapterPort
connection_test_SOURCES = connection-test.cc
