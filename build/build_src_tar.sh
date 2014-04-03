#!/bin/bash
#

set -e

# Must be executed from the outside.

mkdir ./petmp
cd petmp
git clone https://github.com/flonatel/pipexec.git
cd pipexec
git checkout directedgraphofpipes
rm -fr .git
cd ..
tar -cvf pipexec.tar pipexec
xz -9 pipexec.tar
cd ..
mv petmp/pipexec.tar.xz .
rm -fr ./petmp


