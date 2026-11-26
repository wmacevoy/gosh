# Forwarding: forwards and the stream, carried over the side channel (design v2, 2.2).
# gosh's own file. tools/build.sh copies this directory into its copy of the core, and
# seam/05-build.patch tells the core's build that it is there.
AM_CPPFLAGS = -I$(top_srcdir)/ $(protobuf_CFLAGS) $(CRYPTO_CFLAGS)
AM_CXXFLAGS = $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)

# One library for the part; its three kinds of forward are subdirectories (tcp/, udp/, stream/),
# each with its own README and unit tests. Objects are built beside their sources.
AUTOMAKE_OPTIONS = subdir-objects

noinst_LIBRARIES = libgoshforwarding.a

libgoshforwarding_a_SOURCES = forwarding.h rules.h stream.h streamchild.h \
  imp/forward.cc imp/forward.h imp/frame.cc imp/frame.h imp/tcp/bytes.h imp/tcp/connection.h imp/tcp/early.h imp/tcp/held.h imp/tcp/options.h imp/tcp/timer.h imp/tcp/tcpadapter.cc imp/tcp/tcpadapter.h imp/tcp/tcpforward.cc imp/tcp/tcpforward.h imp/tcp/tcpinudp.cc imp/tcp/tcpinudp.h imp/udp/conversations.h imp/udp/sending.h imp/udp/receiving.h imp/udp/fragments.cc imp/udp/fragments.h imp/udp/udpforward.cc imp/udp/udpforward.h imp/stream/streamchild.cc

# A unit test is named for the file it tests, with -test. Those of tcpforward, tcpinudp and
# udpforward are the fork's, which drive the classes on real sockets; the others are logic
# alone, with no socket and no clock.
check_PROGRAMS = imp/tcp/bytes-test imp/tcp/options-test imp/tcp/held-test imp/tcp/early-test imp/tcp/timer-test imp/tcp/connection-test imp/tcp/tcpforward-test imp/tcp/tcpinudp-test imp/udp/sending-test imp/udp/receiving-test imp/udp/conversations-test imp/udp/udpforward-test imp/stream/stream-test imp/frame-fuzzer imp/tcp/tcpinudp-fuzzer imp/tcp/tcpadapter-fuzzer imp/udp/udpforward-fuzzer
TESTS = $(check_PROGRAMS)
LDADD = libfacts.a libgoshforwarding.a ../network/libmoshnetwork.a ../crypto/libmoshcrypto.a ../util/libmoshutil.a $(CRYPTO_LIBS)
imp_tcp_bytes_test_SOURCES = imp/tcp/bytes-test.cc
imp_tcp_options_test_SOURCES = imp/tcp/options-test.cc
imp_tcp_held_test_SOURCES = imp/tcp/held-test.cc
imp_tcp_early_test_SOURCES = imp/tcp/early-test.cc
imp_tcp_timer_test_SOURCES = imp/tcp/timer-test.cc
imp_tcp_connection_test_SOURCES = imp/tcp/connection-test.cc
imp_tcp_tcpforward_test_SOURCES = imp/tcp/tcpforward-test.cc
imp_tcp_tcpinudp_test_SOURCES = imp/tcp/tcpinudp-test.cc
imp_udp_sending_test_SOURCES = imp/udp/sending-test.cc
imp_udp_receiving_test_SOURCES = imp/udp/receiving-test.cc
imp_udp_conversations_test_SOURCES = imp/udp/conversations-test.cc
imp_udp_udpforward_test_SOURCES = imp/udp/udpforward-test.cc
imp_stream_stream_test_SOURCES = imp/stream/stream-test.cc

# Fuzzers (queue item 21): each is built twice. Here, with the ordinary compiler, it replays its
# corpus (the directory beside it named for it, with -corpus after), so an input that once broke
# the part breaks the check. In CI, with clang and -DGOSH_LIBFUZZER, libFuzzer runs it.
imp_frame_fuzzer_SOURCES = imp/frame-fuzzer.cc
imp_tcp_tcpinudp_fuzzer_SOURCES = imp/tcp/tcpinudp-fuzzer.cc
imp_tcp_tcpadapter_fuzzer_SOURCES = imp/tcp/tcpadapter-fuzzer.cc
imp_udp_udpforward_fuzzer_SOURCES = imp/udp/udpforward-fuzzer.cc

# The testing library (vendor/facts/README.md), built here for the tests alone, without gosh's
# warning flags: it is not gosh's code. (subdir-objects: its source is outside this directory.)
check_LIBRARIES = libfacts.a
libfacts_a_SOURCES = ../../vendor/facts/facts.cpp
libfacts_a_CXXFLAGS =
