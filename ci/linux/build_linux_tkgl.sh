#!/bin/bash
set -e
TKGL_VERSION=1.2.1
TCL8_VERSION=8.6.15
TCL9_VERSION=9.0.0
TCL8_URL=https://prdownloads.sourceforge.net/tcl/tcl$TCL8_VERSION-src.tar.gz
TK8_URL=https://prdownloads.sourceforge.net/tcl/tk$TCL8_VERSION-src.tar.gz
TCL9_URL=https://prdownloads.sourceforge.net/tcl/tcl$TCL9_VERSION-src.tar.gz
TK9_URL=https://prdownloads.sourceforge.net/tcl/tk$TCL9_VERSION-src.tar.gz

mkdir -p dist/Tkgl$TKGL_VERSION
rm -rf tcl9 tk9 tcl8 tk8
mkdir tcl9 tk9 tcl8 tk8

echo downloading tcl/tk source ...
wget --no-check-certificate -O tcl8.tar.gz $TCL8_URL
wget --no-check-certificate -O tk8.tar.gz $TK8_URL
wget --no-check-certificate -O tcl9.tar.gz $TCL9_URL
wget --no-check-certificate -O tk9.tar.gz $TK9_URL

echo extracting source ...
tar xf tcl8.tar.gz --directory=tcl8 --strip-components=1
tar xf tk8.tar.gz --directory=tk8 --strip-components=1
tar xf tcl9.tar.gz --directory=tcl9 --strip-components=1
tar xf tk9.tar.gz --directory=tk9 --strip-components=1
echo building tcl and tk
cd tcl8/unix
./configure
make -j4
cd ../../tk8/unix
./configure --with-tcl=../../tcl8/unix
make -j4
cd ../..
cd tcl9/unix
./configure
make -j4
cd ../../tk9/unix
./configure --with-tcl=../../tcl9/unix
make -j4
cd ../..
cp ci/linux/linux_configure configure
echo building for tcl/tk 8
./configure --with-tcl=tcl8/unix --with-tk=tk8/unix
make
mv libTkgl*.so dist/Tkgl$TKGL_VERSION
rm -rf build
echo building for tcl/tk 9
./configure --with-tcl=tcl9/unix --with-tk=tk9/unix
make
mv *.so dist/Tkgl$TKGL_VERSION
mv pkgIndex.tcl dist/Tkgl$TKGL_VERSION
