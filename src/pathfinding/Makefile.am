# path-finding (design v2, 2.2). The fork's files, carried unchanged for relay to build against;
# queue item 8 lands the part. Until then gosh's client starts its relay with path-finding off.
AM_CPPFLAGS = -I$(top_srcdir)/ $(protobuf_CFLAGS) $(CRYPTO_CFLAGS)
AM_CXXFLAGS = $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = libgoshpathfinding.a

libgoshpathfinding_a_SOURCES = punch.cc punch.h
