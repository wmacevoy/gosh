# Copy: one end of a file copy over a stream (design v2, 2.2). gosh's own file.
AM_CPPFLAGS = -I$(top_srcdir)/ $(protobuf_CFLAGS) $(CRYPTO_CFLAGS)
AM_CXXFLAGS = $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = libgoshcopy.a

# The logic, each a header with its test: options, messages, names, checksum, decisions. The edge:
# filecopy.cc, the sender and the receiver.
libgoshcopy_a_SOURCES = filecopy.h options.h imp/filecopy.cc imp/messages.h imp/names.h imp/checksum.h imp/decisions.h

# A test for each piece of logic, with plain values; and filecopy-test, the fork's test of the
# two ends joined by a socket pair, which now tests the edge.
check_PROGRAMS = imp/options-test imp/messages-test imp/names-test imp/checksum-test imp/decisions-test imp/filecopy-test imp/filecopy-fuzzer imp/sender-fuzzer
TESTS = $(check_PROGRAMS)
imp_options_test_SOURCES = imp/options-test.cc
imp_options_test_LDADD = libfacts.a
imp_messages_test_SOURCES = imp/messages-test.cc
imp_messages_test_LDADD = libfacts.a
imp_names_test_SOURCES = imp/names-test.cc
imp_names_test_LDADD = libfacts.a
imp_checksum_test_SOURCES = imp/checksum-test.cc
imp_checksum_test_LDADD = libfacts.a
imp_decisions_test_SOURCES = imp/decisions-test.cc
imp_decisions_test_LDADD = libfacts.a
imp_filecopy_test_SOURCES = imp/filecopy-test.cc
imp_filecopy_test_LDADD = libfacts.a libgoshcopy.a

# Fuzzers (queue item 21): each is built twice. Here, with the ordinary compiler, it replays its
# corpus (the directory beside it named for it, with -corpus after), so an input that once broke
# the part breaks the check. In CI, with clang and -DGOSH_LIBFUZZER, libFuzzer runs it.
imp_filecopy_fuzzer_SOURCES = imp/filecopy-fuzzer.cc
imp_filecopy_fuzzer_LDADD = libgoshcopy.a
imp_sender_fuzzer_SOURCES = imp/sender-fuzzer.cc
imp_sender_fuzzer_LDADD = libgoshcopy.a

AUTOMAKE_OPTIONS = subdir-objects

# The testing library (vendor/facts/README.md), built here for the tests alone, without gosh's
# warning flags: it is not gosh's code. (subdir-objects: its source is outside this directory.)
check_LIBRARIES = libfacts.a
libfacts_a_SOURCES = ../../vendor/facts/facts.cpp
libfacts_a_CXXFLAGS =
