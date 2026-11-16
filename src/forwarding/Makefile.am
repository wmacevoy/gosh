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
  tcp/bytes.h tcp/connection.h tcp/early.h tcp/held.h tcp/options.h tcp/timer.h tcp/tcpadapter.cc tcp/tcpadapter.h tcp/tcpforward.cc tcp/tcpforward.h tcp/tcpinudp.cc tcp/tcpinudp.h \
  udp/conversations.h udp/rules.h udp/sending.h udp/receiving.h udp/fragments.cc udp/fragments.h udp/udpforward.cc udp/udpforward.h \
  stream/stream.h stream/streamchild.cc stream/streamchild.h

# A unit test is named for the file it tests, with -test. Those of tcpforward, tcpinudp and
# udpforward are the fork's, which drive the classes on real sockets; the others are logic
# alone, with no socket and no clock.
check_PROGRAMS = tcp/bytes-test tcp/options-test tcp/held-test tcp/early-test tcp/timer-test tcp/connection-test tcp/tcpforward-test tcp/tcpinudp-test udp/sending-test udp/receiving-test udp/conversations-test udp/udpforward-test stream/stream-test \
  frame-fuzzer tcp/tcpinudp-fuzzer tcp/tcpadapter-fuzzer udp/udpforward-fuzzer
TESTS = $(check_PROGRAMS)
LDADD = libgoshforwarding.a ../copy/libgoshcopy.a ../network/libmoshnetwork.a ../crypto/libmoshcrypto.a ../util/libmoshutil.a $(CRYPTO_LIBS)
tcp_bytes_test_SOURCES = tcp/bytes-test.cc
tcp_options_test_SOURCES = tcp/options-test.cc
tcp_held_test_SOURCES = tcp/held-test.cc
tcp_early_test_SOURCES = tcp/early-test.cc
tcp_timer_test_SOURCES = tcp/timer-test.cc
tcp_connection_test_SOURCES = tcp/connection-test.cc
tcp_tcpforward_test_SOURCES = tcp/tcpforward-test.cc
tcp_tcpinudp_test_SOURCES = tcp/tcpinudp-test.cc
udp_sending_test_SOURCES = udp/sending-test.cc
udp_receiving_test_SOURCES = udp/receiving-test.cc
udp_conversations_test_SOURCES = udp/conversations-test.cc
udp_udpforward_test_SOURCES = udp/udpforward-test.cc
stream_stream_test_SOURCES = stream/stream-test.cc

# Fuzzers (queue item 21): each is built twice. Here, with the ordinary compiler, it replays its
# corpus (the directory beside it named for it, with -corpus after), so an input that once broke
# the part breaks the check. In CI, with clang and -DGOSH_LIBFUZZER, libFuzzer runs it.
frame_fuzzer_SOURCES = frame-fuzzer.cc
tcp_tcpinudp_fuzzer_SOURCES = tcp/tcpinudp-fuzzer.cc
tcp_tcpadapter_fuzzer_SOURCES = tcp/tcpadapter-fuzzer.cc
udp_udpforward_fuzzer_SOURCES = udp/udpforward-fuzzer.cc
