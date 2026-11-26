# relay (design v2, 2.2): the session carried over ssh when UDP cannot get there (REQ1/PATH-1, REQ1/PATH-2).
AM_CPPFLAGS = -I$(top_srcdir)/ $(protobuf_CFLAGS) $(CRYPTO_CFLAGS)
AM_CXXFLAGS = $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = libgoshrelay.a

libgoshrelay_a_SOURCES = relay.h start.h imp/local.cc imp/remote.cc imp/process.h imp/paths.h imp/frame.h imp/settings.h imp/whose.h

check_PROGRAMS = imp/start-test imp/paths-test imp/frame-test imp/settings-test imp/whose-test imp/frame-fuzzer
TESTS = $(check_PROGRAMS)
imp_start_test_SOURCES = imp/start-test.cc
imp_start_test_LDADD = libfacts.a
imp_paths_test_SOURCES = imp/paths-test.cc
imp_paths_test_LDADD = libfacts.a
imp_frame_test_SOURCES = imp/frame-test.cc
imp_frame_test_LDADD = libfacts.a
imp_settings_test_SOURCES = imp/settings-test.cc
imp_settings_test_LDADD = libfacts.a
imp_whose_test_SOURCES = imp/whose-test.cc
imp_whose_test_LDADD = libfacts.a

# A fuzzer (queue item 21, its review): built with the ordinary compiler it replays its corpus
# (frame-fuzzer-corpus/); in CI, with clang and -DGOSH_LIBFUZZER, libFuzzer runs it.
imp_frame_fuzzer_SOURCES = imp/frame-fuzzer.cc
imp_frame_fuzzer_LDADD = ../crypto/libmoshcrypto.a ../util/libmoshutil.a $(CRYPTO_LIBS)

AUTOMAKE_OPTIONS = subdir-objects

# The testing library (vendor/facts/README.md), built here for the tests alone, without gosh's
# warning flags: it is not gosh's code. (subdir-objects: its source is outside this directory.)
check_LIBRARIES = libfacts.a
libfacts_a_SOURCES = ../../vendor/facts/facts.cpp
libfacts_a_CXXFLAGS =
