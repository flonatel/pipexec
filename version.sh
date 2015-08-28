#!/bin/bash
set -e

TOPSRCDIR=""
if test $# -eq 1;
then
    TOPSRCDIR=$1
    cd ${TOPSRCDIR}
fi

VERSION=$(git describe --tags --abbrev=8 HEAD 2>/dev/null)
echo ${VERSION}
