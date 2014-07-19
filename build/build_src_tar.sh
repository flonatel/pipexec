#!/bin/bash
#

VERSION=2.4

set -e

# Must be executed from the outside.

mkdir ./petmp
cd petmp
git clone ../pipexec
cd pipexec
git checkout master
rm -fr .git
cd ..
mv pipexec pipexec-${VERSION}
tar -cvf pipexec-${VERSION}.tar pipexec-${VERSION}
xz -9 pipexec-${VERSION}.tar
cd ..
mv petmp/pipexec-${VERSION}.tar.xz .
rm -fr ./petmp


