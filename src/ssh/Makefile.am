# ssh (2026-11-26, the owner: "ssh is its own domain"): what gosh reads of ssh, its config and the
# connection it made, apart from the command and the relay that use it (REQ6/SHAPE-5).
AM_CPPFLAGS = -I$(top_srcdir)/
AM_CXXFLAGS = $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = libgoshssh.a

libgoshssh_a_SOURCES = config.h connection.h imp/config.cc

check_PROGRAMS = imp/config-test
TESTS = $(check_PROGRAMS)
LDADD = libfacts.a libgoshssh.a
imp_config_test_SOURCES = imp/config-test.cc

AUTOMAKE_OPTIONS = subdir-objects

# The testing library (vendor/facts/README.md), built here for the tests alone, without gosh's
# warning flags: it is not gosh's code. (subdir-objects: its source is outside this directory.)
check_LIBRARIES = libfacts.a
libfacts_a_SOURCES = ../../vendor/facts/facts.cpp
libfacts_a_CXXFLAGS =
