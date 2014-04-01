#
# This is the Master-Makefile
#
# Rules:
#  o NEVER EVER use recursive Make (cd xxx && make)
#    Use include instead
#  o Do not put rules / dependencies in this file:
#    Instead use the Makefile parts and include them
#    from here
#  o If unsure: ask!
#

ACLOCAL_AMFLAGS=-I m4

bin_PROGRAMS = 
lib_LTLIBRARIES =
noinst_PROGRAMS =
noinst_LTLIBRARIES =

BUILT_SOURCES =
CLEANFILES =
TESTS = 

# Generics
include src/Makefile.inc
include test/Makefile.inc
