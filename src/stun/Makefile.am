# STUN (REQ15/PATH-4; docs/PROPOSAL_domain-map.md): the question "what address do I appear to have
# from outside?" and its answer, as bytes from anyone, for path-finding and the meetup. Headers
# alone; what reads an answer is proved (src/strong/stun/). Built here: its tests.
AM_CPPFLAGS = -I$(top_srcdir)/ $(protobuf_CFLAGS) $(CRYPTO_CFLAGS)
AM_CXXFLAGS = $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)

check_PROGRAMS = imp/stun-test imp/stun-fuzzer
TESTS = $(check_PROGRAMS)
LDADD = libfacts.a ../network/libmoshnetwork.a ../crypto/libmoshcrypto.a ../util/libmoshutil.a $(CRYPTO_LIBS)
imp_stun_test_SOURCES = imp/stun-test.cc

# The fuzzer (queue item 21): built here with the ordinary compiler, it replays its corpus
# (stun-fuzzer-corpus/); in CI, with clang and -DGOSH_LIBFUZZER, libFuzzer runs it.
imp_stun_fuzzer_SOURCES = imp/stun-fuzzer.cc

AUTOMAKE_OPTIONS = subdir-objects

# The testing library (vendor/facts/README.md), built here for the tests alone, without gosh's
# warning flags: it is not gosh's code.
check_LIBRARIES = libfacts.a
libfacts_a_SOURCES = ../../vendor/facts/facts.cpp
libfacts_a_CXXFLAGS =
