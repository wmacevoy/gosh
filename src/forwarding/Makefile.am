# Forwarding: forwards and the stream, carried over the side channel (design v2, 2.2).
# gosh's own file. tools/build.sh copies this directory into its copy of the core, and
# seam/05-build.patch tells the core's build that it is there.
AM_CPPFLAGS = -I$(top_srcdir)/ $(protobuf_CFLAGS) $(CRYPTO_CFLAGS)
AM_CXXFLAGS = $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)

# One library for the part; its three kinds of forward are subdirectories (tcp/, udp/, stream/),
# each with its own README and unit tests. Objects are built beside their sources.
AUTOMAKE_OPTIONS = subdir-objects

noinst_LIBRARIES = libgoshforwarding.a

libgoshforwarding_a_SOURCES = forwarding.h forward.cc forward.h frame.cc frame.h \
  tcp/connection.h tcp/tcpadapter.cc tcp/tcpadapter.h tcp/tcpforward.cc tcp/tcpforward.h tcp/tcpinudp.cc tcp/tcpinudp.h \
  udp/conversations.h udp/fragments.cc udp/fragments.h udp/udpforward.cc udp/udpforward.h \
  stream/stream.h stream/streamchild.cc stream/streamchild.h

# A unit test is named for the file it tests, with -test. Those of tcpforward, tcpinudp and
# udpforward are the fork's, which drive the classes on real sockets; the others are logic
# alone, with no socket and no clock.
check_PROGRAMS = tcp/connection-test tcp/tcpforward-test tcp/tcpinudp-test udp/conversations-test udp/udpforward-test stream/stream-test
TESTS = $(check_PROGRAMS)
LDADD = libgoshforwarding.a ../copy/libgoshcopy.a ../network/libmoshnetwork.a ../crypto/libmoshcrypto.a ../util/libmoshutil.a $(CRYPTO_LIBS)
tcp_connection_test_SOURCES = tcp/connection-test.cc
tcp_tcpforward_test_SOURCES = tcp/tcpforward-test.cc
tcp_tcpinudp_test_SOURCES = tcp/tcpinudp-test.cc
udp_conversations_test_SOURCES = udp/conversations-test.cc
udp_udpforward_test_SOURCES = udp/udpforward-test.cc
stream_stream_test_SOURCES = stream/stream-test.cc
