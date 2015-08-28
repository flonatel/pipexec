#!/bin/bash
#
# Create release tarball
#

PKGBUILDDIR="../pbuild"

set -e

if test $# -ne 1;
then
    echo "Usage: create_tar.sh <ReleaseNum>"
    exit 1
fi

RELNUM=$1

rm -fr ${PKGBUILDDIR}
mkdir -p ${PKGBUILDDIR}

git tag ${RELNUM}
git archive --format=tar --prefix=pipexec-${RELNUM}/ ${RELNUM} | tar -C ${PKGBUILDDIR} -xf -

cd ${PKGBUILDDIR}/pipexec-${RELNUM}

echo ${RELNUM} >version.txt
bash ./build/init_autotools.sh

cd ..
tar -cf - pipexec-${RELNUM} | xz -c -9 >pipexec-${RELNUM}.tar.xz

