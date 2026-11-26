# path-finding (design v2, 2.2): a way straight between the two relays through NATs, or through
# the user's meetup with a ticket (REQ1/PATH-3, REQ15/PATH-4). The fork's punch.*, its meetup half
# replaced by tickets.
AM_CPPFLAGS = -I$(top_srcdir)/ $(protobuf_CFLAGS) $(CRYPTO_CFLAGS)
AM_CXXFLAGS = $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = libgoshpathfinding.a

libgoshpathfinding_a_SOURCES = punch.h mark.h got.h imp/finding.h imp/punch.cc

check_PROGRAMS = imp/finding-test imp/punch-test imp/finding-fuzzer
TESTS = $(check_PROGRAMS)
LDADD = libfacts.a libgoshpathfinding.a ../meetup/libgoshmeetup.a ../network/libmoshnetwork.a ../crypto/libmoshcrypto.a ../util/libmoshutil.a $(CRYPTO_LIBS)
imp_finding_test_SOURCES = imp/finding-test.cc
imp_punch_test_SOURCES = imp/punch-test.cc

# Fuzzers (queue item 21): each is built twice. Here, with the ordinary compiler, it replays its
# corpus (the directory beside it named for it, with -corpus after), so an input that once broke
# the part breaks the check. In CI, with clang and -DGOSH_LIBFUZZER, libFuzzer runs it.
imp_finding_fuzzer_SOURCES = imp/finding-fuzzer.cc

AUTOMAKE_OPTIONS = subdir-objects

# The testing library (vendor/facts/README.md), built here for the tests alone, without gosh's
# warning flags: it is not gosh's code. (subdir-objects: its source is outside this directory.)
check_LIBRARIES = libfacts.a
libfacts_a_SOURCES = ../../vendor/facts/facts.cpp
libfacts_a_CXXFLAGS =
