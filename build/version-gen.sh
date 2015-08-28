#!/bin/sh
set -e

TOPSRCDIR=""
if test $# -eq 1;
then
    TOPSRCDIR=$1/
fi

VERSION=$(${TOPSRCDIR}version.sh $@)

OFILE=${PWD}/src/app_version.c

cat <<EOF >${OFILE}
#include "src/version.h"
char const app_version[] = "${VERSION}";
EOF
