#!/bin/bash
#
# This must be called from within the top source dir.
#

libtoolize --copy

aclocal -I m4
autoheader
automake --add-missing --copy
automake
autoconf
