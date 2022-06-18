#!/bin/sh
#
# This must be called from within the top source dir.
#

set -e
set -x

bash -x ./version.sh

libtoolize --copy

aclocal -I m4
autoheader
automake --add-missing --copy
automake
autoconf
