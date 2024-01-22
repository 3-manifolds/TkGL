#!/usr/bin/bash
set -e
TCL8_VERSION=8.6.13
TCL9_VERSION=9.0b1
WGET="/c/msys64/usr/bin/wget -nv"
CMD=/c/msys64/usr/bin/cmd
if [ ! -e tcl$TCL8_VERSION-src.tar.gz ] ; then
    $WGET https://prdownloads.sourceforge.net/tcl/tcl$TCL8_VERSION-src.tar.gz
fi
if [ ! -e tk$TCL8_VERSION-src.tar.gz ]; then
    $WGET https://prdownloads.sourceforge.net/tcl/tk$TCL8_VERSION-src.tar.gz
fi
if [ ! -e tcl$TCL9_VERSION-src.tar.gz ] ; then
    $WGET https://prdownloads.sourceforge.net/tcl/tcl$TCL9_VERSION-src.tar.gz
fi
if [ ! -e tk$TCL9_VERSION-src.tar.gz ]; then
    $WGET https://prdownloads.sourceforge.net/tcl/tk$TCL9_VERSION-src.tar.gz
fi
rm -rf tcl8 tk8 tcl9 tk9
tar xfz tcl$TCL8_VERSION-src.tar.gz
mv tcl$TCL8_VERSION tcl8
tar xfz tk$TCL8_VERSION-src.tar.gz
mv tk$TCL8_VERSION tk8
tar xfz tcl$TCL9_VERSION-src.tar.gz
mv tcl$TCL9_VERSION tcl9
tar xfz tk$TCL9_VERSION-src.tar.gz
mv tk$TCL9_VERSION tk9

$CMD < ci/windows/build_windows_tkgl.bat
#rm -rf dist
#mkdir -p dist/Tkgl1.0
#mv win/Release*/*.* Tkgl1.0
