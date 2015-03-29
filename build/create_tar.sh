#!/bin/bash
#
# Create release tarball
#

set -e

if test $# -ne 1;
then
    echo "Usage: create_tar.sh <ReleaseNum>"
    exit 1
fi

RELNUM=$1

rm -fr pbuild
mkdir -p pbuild

git tag ${RELNUM}
git archive --format=tar --prefix=pipexec-${RELNUM}/ ${RELNUM} | tar -C pbuild -xf -

cd pbuild/pipexec-${RELNUM}

bash ./build/init_autotools.sh

cd ..
tar -cf - pipexec-${RELNUM} | xz -c -9 >pipexec-${RELNUM}.tar.xz

