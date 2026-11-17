# relay (design v2, 2.2): the session carried over ssh when UDP cannot get there (REQ1/PATH-1, REQ1/PATH-2).
AM_CPPFLAGS = -I$(top_srcdir)/ $(protobuf_CFLAGS) $(CRYPTO_CFLAGS)
AM_CXXFLAGS = $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = libgoshrelay.a

libgoshrelay_a_SOURCES = local.cc remote.cc process.h relay.h start.h paths.h frame.h settings.h whose.h

check_PROGRAMS = start-test paths-test frame-test settings-test whose-test frame-fuzzer
TESTS = $(check_PROGRAMS)
start_test_SOURCES = start-test.cc
start_test_LDADD = libfacts.a
paths_test_SOURCES = paths-test.cc
paths_test_LDADD = libfacts.a
frame_test_SOURCES = frame-test.cc
frame_test_LDADD = libfacts.a
settings_test_SOURCES = settings-test.cc
settings_test_LDADD = libfacts.a
whose_test_SOURCES = whose-test.cc
whose_test_LDADD = libfacts.a

# A fuzzer (queue item 21, its review): built with the ordinary compiler it replays its corpus
# (frame-fuzzer-corpus/); in CI, with clang and -DGOSH_LIBFUZZER, libFuzzer runs it.
frame_fuzzer_SOURCES = frame-fuzzer.cc
frame_fuzzer_LDADD = ../crypto/libmoshcrypto.a ../util/libmoshutil.a $(CRYPTO_LIBS)

AUTOMAKE_OPTIONS = subdir-objects

# The testing library (vendor/facts/README.md), built here for the tests alone, without gosh's
# warning flags: it is not gosh's code. (subdir-objects: its source is outside this directory.)
check_LIBRARIES = libfacts.a
libfacts_a_SOURCES = ../../vendor/facts/facts.cpp
libfacts_a_CXXFLAGS =
