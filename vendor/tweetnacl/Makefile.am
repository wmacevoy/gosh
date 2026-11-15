# TweetNaCl, carried (vendor/tweetnacl/README.md). gosh's own file. tools/build.sh copies this
# directory into its copy of the core as src/tweetnacl, and seam/05-build.patch tells the core's
# build that it is there. Compiled as C, with none of gosh's warning flags: the file is not
# edited, so that it stays the file its authors published.
noinst_LIBRARIES = libtweetnacl.a
libtweetnacl_a_SOURCES = tweetnacl.c tweetnacl.h

check_PROGRAMS = tweetnacl-test
TESTS = $(check_PROGRAMS)
tweetnacl_test_SOURCES = tweetnacl-test.c
tweetnacl_test_LDADD = libtweetnacl.a
