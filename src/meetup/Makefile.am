# meetup (design v2, 2.2): a machine with a public address where two ends behind NATs find each
# other, and through which, with its key, their datagrams pass (REQ15/PATH-4, REQ15/PATH-5).
AM_CPPFLAGS = -I$(top_srcdir)/ $(protobuf_CFLAGS) $(CRYPTO_CFLAGS)
AM_CXXFLAGS = $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = libgoshmeetup.a

libgoshmeetup_a_SOURCES = proof.cc proof.h passing.h answer.h serve.cc meetup.h

check_PROGRAMS = proof-test passing-test answer-test
TESTS = $(check_PROGRAMS)
LDADD = libgoshmeetup.a ../crypto/libmoshcrypto.a ../util/libmoshutil.a $(CRYPTO_LIBS)
proof_test_SOURCES = proof-test.cc
passing_test_SOURCES = passing-test.cc
answer_test_SOURCES = answer-test.cc
