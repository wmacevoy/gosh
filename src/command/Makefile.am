# The command: gosh and gocp (design v2, 2.2). gosh's own file.
AM_CPPFLAGS = -I$(top_srcdir)/ $(protobuf_CFLAGS) $(CRYPTO_CFLAGS)
AM_CXXFLAGS = $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = libgoshcommand.a

# logic: options, words, startup, stock; edges: login, proxy; and moshwrapper, which strings them together
libgoshcommand_a_SOURCES = moshwrapper.cc moshwrapper.h options.cc options.h words.cc words.h startup.cc startup.h \
	stock.cc stock.h login.cc login.h proxy.cc proxy.h

check_PROGRAMS = stock-test options-test words-test startup-test
TESTS = $(check_PROGRAMS)
stock_test_SOURCES = stock-test.cc
stock_test_LDADD = libgoshcommand.a
options_test_SOURCES = options-test.cc
options_test_LDADD = libgoshcommand.a
words_test_SOURCES = words-test.cc
words_test_LDADD = libgoshcommand.a
startup_test_SOURCES = startup-test.cc
startup_test_LDADD = libgoshcommand.a
