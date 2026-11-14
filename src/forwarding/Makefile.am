# Forwarding: forwards and the stream, carried over the side channel (design v2, 2.2).
# gosh's own file. tools/build.sh copies this directory into its copy of the core, and
# seam/05-build.patch tells the core's build that it is there.
AM_CPPFLAGS = -I$(top_srcdir)/ $(protobuf_CFLAGS) $(CRYPTO_CFLAGS)
AM_CXXFLAGS = $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = libgoshforwarding.a

libgoshforwarding_a_SOURCES = forwarding.h udpforward.cc udpforward.h tcpforward.cc tcpforward.h tcpadapter.cc tcpadapter.h tcpinudp.cc tcpinudp.h connection.h

# The first three are the fork's tests of these classes, which call them directly: they hold
# the carried code to what it did at pf.7, and go as each piece is reshaped. connection-test is
# the connection logic by itself, with no socket and no clock.
check_PROGRAMS = udp-forward tcp-in-udp tcp-forward connection-test
TESTS = $(check_PROGRAMS)
LDADD = libgoshforwarding.a ../network/libmoshnetwork.a ../crypto/libmoshcrypto.a ../util/libmoshutil.a $(CRYPTO_LIBS)
udp_forward_SOURCES = udp-forward.cc
tcp_in_udp_SOURCES = tcp-in-udp.cc
tcp_forward_SOURCES = tcp-forward.cc
connection_test_SOURCES = connection-test.cc
