# The seam: gosh's code that meets the core (design v2, section 4). gosh's own file.
# The patches in seam/ change the core; this directory is what those changes call.
AM_CPPFLAGS = -I$(top_srcdir)/ $(protobuf_CFLAGS) $(CRYPTO_CFLAGS)
AM_CXXFLAGS = $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)

# Three libraries, because each program has a main of gosh's own and one library
# cannot hold two: the linker takes main from the one its program names.
noinst_LIBRARIES = libgoshseam.a libgoshclientmain.a libgoshservermain.a

libgoshseam_a_SOURCES = options.cc options.h session.cc hooks.h core-main.h
libgoshclientmain_a_SOURCES = client-main.cc
libgoshservermain_a_SOURCES = server-main.cc

check_PROGRAMS = options-test
TESTS = $(check_PROGRAMS)
options_test_SOURCES = options-test.cc
options_test_LDADD = libgoshseam.a
