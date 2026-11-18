# meetup (design v2, 2.2): a machine with a public address where two ends behind NATs find each
# other, and through which, with a ticket, their datagrams pass (REQ15/PATH-4, REQ18/PATH-5).
AM_CPPFLAGS = -I$(top_srcdir)/ $(protobuf_CFLAGS) $(CRYPTO_CFLAGS)
AM_CXXFLAGS = $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = libgoshmeetup.a

libgoshmeetup_a_SOURCES = ticket.h counter.h messages.h registrations.h answer.h master.cc master.h serve.cc meetup.h

check_PROGRAMS = ticket-test messages-test registrations-test answer-test registrations-fuzzer
TESTS = $(check_PROGRAMS)
LDADD = libfacts.a libgoshmeetup.a ../crypto/libmoshcrypto.a ../util/libmoshutil.a $(CRYPTO_LIBS)
ticket_test_SOURCES = ticket-test.cc
messages_test_SOURCES = messages-test.cc
registrations_test_SOURCES = registrations-test.cc

# The testing library (vendor/facts/README.md), built here for the tests alone, without gosh's
# warning flags: it is not gosh's code. (subdir-objects: its source is outside this directory.)
AUTOMAKE_OPTIONS = subdir-objects
check_LIBRARIES = libfacts.a
libfacts_a_SOURCES = ../../vendor/facts/facts.cpp
libfacts_a_CXXFLAGS =
answer_test_SOURCES = answer-test.cc

# Fuzzers (queue item 21): each is built twice. Here, with the ordinary compiler, it replays its
# corpus (the directory beside it named for it, with -corpus after), so an input that once broke
# the part breaks the check. In CI, with clang and -DGOSH_LIBFUZZER, libFuzzer runs it.
registrations_fuzzer_SOURCES = registrations-fuzzer.cc
