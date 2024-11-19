#!/bin/bash
set -e
TCL8_SOURCE=tcl8.6.15-src.tar.gz
TCL9_SOURCE=tcl9.0.0-src.tar.gz
TK8_SOURCE=tk8.6.15-src.tar.gz
TK9_SOURCE=tk9.0.0-src.tar.gz
TCL8_URL=https://prdownloads.sourceforge.net/tcl/$TCL8_SOURCE
TK8_URL=https://prdownloads.sourceforge.net/tcl/$TK8_SOURCE
TCL9_URL=https://prdownloads.sourceforge.net/tcl/$TCL9_SOURCE
TK9_URL=https://prdownloads.sourceforge.net/tcl/$TK9_SOURCE

mkdir -p dist/Tkgl$TKGL_VERSION
rm -rf tcl8 tk8 tcl9 tk9
mkdir tcl8 tk8 tcl9 tk9

if ! [ -e $TCL8_SOURCE ]; then
    wget -nv --no-check-certificate $TCL8_URL
fi
if ! [ -e $TK8_SOURCE ] || ! [ -e tk8 ] ; then
    wget -nv --no-check-certificate $TK8_URL
fi
if ! [ -e $TCL9_SOURCE ] ; then
    wget -nv --no-check-certificate $TCL9_URL
fi
if ! [ -e $TK9_SOURCE ]; then
    wget -nv --no-check-certificate $TK9_URL
fi

tar xf $TCL8_SOURCE --directory=tcl8 --strip-components=1
tar xf $TK8_SOURCE --directory=tk8 --strip-components=1
tar xf $TCL9_SOURCE --directory=tcl9 --strip-components=1
tar xf $TK9_SOURCE --directory=tk9 --strip-components=1

cp ci/macOS/mac_configure configure

cd tcl8
make -j4 -C macosx CFLAGS="-arch x86_64 -arch arm64 -mmacosx-version-min=10.9"
cd ../tk8
make -j4 -C macosx CFLAGS="-arch x86_64 -arch arm64 -mmacosx-version-min=10.9"
cd ..
mv build build8
./configure --with-tcl=build8/tcl/Tcl.framework --with-tk=build8/tk/Tk.framework
make CFLAGS="-arch x86_64 -arch arm64 -mmacosx-version-min=10.9"
mv libTkgl$TKGL_VERSION.dylib dist/Tkgl$TKGL_VERSION

cd tcl9
make -j4 -C macosx CFLAGS="-arch x86_64 -arch arm64 -mmacosx-version-min=10.9"
cd ../tk9/
make -j4 -C macosx CFLAGS="-arch x86_64 -arch arm64 -mmacosx-version-min=10.9"
cd ..
mv build build9
./configure --with-tcl=build9/tcl/Tcl.framework --with-tk=build9/tk/Tk.framework
make CFLAGS="-arch x86_64 -arch arm64 -mmacosx-version-min=10.9"
mv libtcl9Tkgl$TKGL_VERSION.dylib dist/Tkgl$TKGL_VERSION
mv pkgIndex.tcl dist/Tkgl$TKGL_VERSION
