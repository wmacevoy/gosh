# Copy: one end of a file copy over a stream (design v2, 2.2). gosh's own file.
AM_CPPFLAGS = -I$(top_srcdir)/ $(protobuf_CFLAGS) $(CRYPTO_CFLAGS)
AM_CXXFLAGS = $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = libgoshcopy.a

libgoshcopy_a_SOURCES = filecopy.cc filecopy.h

# The fork's test of these functions, which calls them directly. It holds this code to
# what it did at pf.7 while it is carried unchanged; it goes when copy is reshaped (item 9).
check_PROGRAMS = file-copy
TESTS = $(check_PROGRAMS)
file_copy_SOURCES = file-copy.cc
file_copy_LDADD = libgoshcopy.a
