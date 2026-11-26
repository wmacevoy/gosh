# meetup (design v2, 2.2): a machine with a public address where two ends behind NATs find each
# other, and through which, with a ticket, their datagrams pass (REQ15/PATH-4, REQ18/PATH-5).
AM_CPPFLAGS = -I$(top_srcdir)/ $(protobuf_CFLAGS) $(CRYPTO_CFLAGS)
AM_CXXFLAGS = $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = libgoshmeetup.a

libgoshmeetup_a_SOURCES = meetup.h master.h ticket.h end.h imp/messages.h imp/sealed.h imp/registrations.h imp/answer.h imp/master.cc imp/serve.cc

check_PROGRAMS = imp/ticket-test imp/messages-test imp/registrations-test imp/answer-test imp/registrations-fuzzer
TESTS = $(check_PROGRAMS)
LDADD = libfacts.a libgoshmeetup.a ../crypto/libmoshcrypto.a ../util/libmoshutil.a $(CRYPTO_LIBS)
imp_ticket_test_SOURCES = imp/ticket-test.cc
imp_messages_test_SOURCES = imp/messages-test.cc
imp_registrations_test_SOURCES = imp/registrations-test.cc

# The testing library (vendor/facts/README.md), built here for the tests alone, without gosh's
# warning flags: it is not gosh's code. (subdir-objects: its source is outside this directory.)
AUTOMAKE_OPTIONS = subdir-objects
check_LIBRARIES = libfacts.a
libfacts_a_SOURCES = ../../vendor/facts/facts.cpp
libfacts_a_CXXFLAGS =
imp_answer_test_SOURCES = imp/answer-test.cc

# Fuzzers (queue item 21): each is built twice. Here, with the ordinary compiler, it replays its
# corpus (the directory beside it named for it, with -corpus after), so an input that once broke
# the part breaks the check. In CI, with clang and -DGOSH_LIBFUZZER, libFuzzer runs it.
imp_registrations_fuzzer_SOURCES = imp/registrations-fuzzer.cc
