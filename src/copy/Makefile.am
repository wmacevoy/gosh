# Copy: one end of a file copy over a stream (design v2, 2.2). gosh's own file.
AM_CPPFLAGS = -I$(top_srcdir)/ $(protobuf_CFLAGS) $(CRYPTO_CFLAGS)
AM_CXXFLAGS = $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = libgoshcopy.a

# The logic, each a header with its test: options, messages, names, checksum, decisions. The edge:
# filecopy.cc, the sender and the receiver.
libgoshcopy_a_SOURCES = filecopy.cc filecopy.h options.h messages.h names.h checksum.h decisions.h

# A test for each piece of logic, with plain values; and filecopy-test, the fork's test of the
# two ends joined by a socket pair, which now tests the edge.
check_PROGRAMS = options-test messages-test names-test checksum-test decisions-test filecopy-test filecopy-fuzzer sender-fuzzer
TESTS = $(check_PROGRAMS)
options_test_SOURCES = options-test.cc
options_test_LDADD = libfacts.a
messages_test_SOURCES = messages-test.cc
messages_test_LDADD = libfacts.a
names_test_SOURCES = names-test.cc
names_test_LDADD = libfacts.a
checksum_test_SOURCES = checksum-test.cc
checksum_test_LDADD = libfacts.a
decisions_test_SOURCES = decisions-test.cc
decisions_test_LDADD = libfacts.a
filecopy_test_SOURCES = filecopy-test.cc
filecopy_test_LDADD = libfacts.a libgoshcopy.a

# Fuzzers (queue item 21): each is built twice. Here, with the ordinary compiler, it replays its
# corpus (the directory beside it named for it, with -corpus after), so an input that once broke
# the part breaks the check. In CI, with clang and -DGOSH_LIBFUZZER, libFuzzer runs it.
filecopy_fuzzer_SOURCES = filecopy-fuzzer.cc
filecopy_fuzzer_LDADD = libgoshcopy.a
sender_fuzzer_SOURCES = sender-fuzzer.cc
sender_fuzzer_LDADD = libgoshcopy.a

AUTOMAKE_OPTIONS = subdir-objects

# The testing library (vendor/facts/README.md), built here for the tests alone, without gosh's
# warning flags: it is not gosh's code. (subdir-objects: its source is outside this directory.)
check_LIBRARIES = libfacts.a
libfacts_a_SOURCES = ../../vendor/facts/facts.cpp
libfacts_a_CXXFLAGS =
