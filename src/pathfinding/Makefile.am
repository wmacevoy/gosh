# path-finding (design v2, 2.2): a way straight between the two relays through NATs, or through
# the user's meetup with a ticket (REQ1/PATH-3, REQ15/PATH-4). The fork's punch.*, its meetup half
# replaced by tickets.
AM_CPPFLAGS = -I$(top_srcdir)/ $(protobuf_CFLAGS) $(CRYPTO_CFLAGS)
AM_CXXFLAGS = $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = libgoshpathfinding.a

libgoshpathfinding_a_SOURCES = stun.h finding.h punch.cc punch.h

check_PROGRAMS = stun-test finding-test punch-test
TESTS = $(check_PROGRAMS)
LDADD = libgoshpathfinding.a ../meetup/libgoshmeetup.a ../network/libmoshnetwork.a ../crypto/libmoshcrypto.a ../util/libmoshutil.a $(CRYPTO_LIBS)
stun_test_SOURCES = stun-test.cc
finding_test_SOURCES = finding-test.cc
punch_test_SOURCES = punch-test.cc
