# relay (design v2, 2.2): the session carried over ssh when UDP cannot get there (REQ1/PATH-1, REQ1/PATH-2).
AM_CPPFLAGS = -I$(top_srcdir)/ $(protobuf_CFLAGS) $(CRYPTO_CFLAGS)
AM_CXXFLAGS = $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = libgoshrelay.a

libgoshrelay_a_SOURCES = relay.cc relay.h start.h paths.h frame.h settings.h whose.h

check_PROGRAMS = start-test paths-test frame-test settings-test whose-test
TESTS = $(check_PROGRAMS)
start_test_SOURCES = start-test.cc
paths_test_SOURCES = paths-test.cc
frame_test_SOURCES = frame-test.cc
settings_test_SOURCES = settings-test.cc
whose_test_SOURCES = whose-test.cc
