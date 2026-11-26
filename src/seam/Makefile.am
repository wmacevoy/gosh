# The seam: gosh's code that meets the core (design v2, section 4). gosh's own file.
# The patches in seam/ change the core; this directory is what those changes call.
AM_CPPFLAGS = -I$(top_srcdir)/ $(protobuf_CFLAGS) $(CRYPTO_CFLAGS)
AM_CXXFLAGS = $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)

# Three libraries, because each program has a main of gosh's own and one library
# cannot hold two: the linker takes main from the one its program names.
noinst_LIBRARIES = libgoshseam.a libgoshclientmain.a libgoshservermain.a

libgoshseam_a_SOURCES = hooks.h core-main.h asked.h imp/applications.cc imp/applications.h imp/options.cc imp/options.h imp/session.cc imp/session.h imp/streaming.cc imp/streaming.h imp/relaying.cc imp/relaying.h
libgoshclientmain_a_SOURCES = imp/client-main.cc
libgoshservermain_a_SOURCES = imp/server-main.cc

check_PROGRAMS = imp/options-test imp/applications-test
TESTS = $(check_PROGRAMS)
imp_options_test_SOURCES = imp/options-test.cc
imp_options_test_LDADD = libfacts.a libgoshseam.a
imp_applications_test_SOURCES = imp/applications-test.cc
imp_applications_test_LDADD = libfacts.a libgoshseam.a ../copy/libgoshcopy.a ../crypto/libmoshcrypto.a ../util/libmoshutil.a $(CRYPTO_LIBS)

AUTOMAKE_OPTIONS = subdir-objects

# The testing library (vendor/facts/README.md), built here for the tests alone, without gosh's
# warning flags: it is not gosh's code. (subdir-objects: its source is outside this directory.)
check_LIBRARIES = libfacts.a
libfacts_a_SOURCES = ../../vendor/facts/facts.cpp
libfacts_a_CXXFLAGS =
