# The command: gosh and gocp (design v2, 2.2). gosh's own file.
AM_CPPFLAGS = -I$(top_srcdir)/ $(protobuf_CFLAGS) $(CRYPTO_CFLAGS)
AM_CXXFLAGS = $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = libgoshcommand.a

# logic: options, words, startup, stock; edges: login, proxy; and moshwrapper, which strings them together
libgoshcommand_a_SOURCES = moshwrapper.h imp/moshwrapper.cc imp/options.cc imp/options.h imp/words.cc imp/words.h imp/startup.cc imp/startup.h imp/stock.cc imp/stock.h imp/login.cc imp/login.h

check_PROGRAMS = imp/stock-test imp/options-test imp/words-test imp/startup-test
TESTS = $(check_PROGRAMS)
imp_stock_test_SOURCES = imp/stock-test.cc
imp_stock_test_LDADD = libfacts.a libgoshcommand.a ../ssh/libgoshssh.a
imp_options_test_SOURCES = imp/options-test.cc
imp_options_test_LDADD = libfacts.a libgoshcommand.a ../ssh/libgoshssh.a
imp_words_test_SOURCES = imp/words-test.cc
imp_words_test_LDADD = libfacts.a libgoshcommand.a ../ssh/libgoshssh.a
imp_startup_test_SOURCES = imp/startup-test.cc
imp_startup_test_LDADD = libfacts.a libgoshcommand.a ../ssh/libgoshssh.a

AUTOMAKE_OPTIONS = subdir-objects

# The testing library (vendor/facts/README.md), built here for the tests alone, without gosh's
# warning flags: it is not gosh's code. (subdir-objects: its source is outside this directory.)
check_LIBRARIES = libfacts.a
libfacts_a_SOURCES = ../../vendor/facts/facts.cpp
libfacts_a_CXXFLAGS =
