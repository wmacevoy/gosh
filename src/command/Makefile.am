# The command: gosh and gocp (design v2, 2.2). gosh's own file.
AM_CPPFLAGS = -I$(top_srcdir)/ $(protobuf_CFLAGS) $(CRYPTO_CFLAGS)
AM_CXXFLAGS = $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = libgoshcommand.a

libgoshcommand_a_SOURCES = moshwrapper.cc moshwrapper.h stock.cc stock.h

check_PROGRAMS = stock-test
TESTS = $(check_PROGRAMS)
stock_test_SOURCES = stock-test.cc
stock_test_LDADD = libgoshcommand.a
