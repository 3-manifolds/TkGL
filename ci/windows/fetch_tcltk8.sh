#!/usr/bin/bash
set -e
TCL8_VERSION=8.6.15
TK8_VERSION=8.6.15
# Find wget; on the ARM runners it's in the second location.
WGET="/c/msys64/usr/bin/wget"
if [ ! -e $WGET ]; then
    WGET="/usr/bin/wget"
fi
if [ ! -e tcl$TCL8_VERSION-src.tar.gz ]; then
    $WGET -nv https://prdownloads.sourceforge.net/tcl/tcl$TCL8_VERSION-src.tar.gz
fi
if [ ! -e tk$TK8_VERSION-src.tar.gz ]; then
    $WGET -nv https://prdownloads.sourceforge.net/tcl/tk$TCL8_VERSION-src.tar.gz
fi
rm -rf tcl8 tk8
tar xfz tcl$TCL8_VERSION-src.tar.gz
mv tcl$TCL8_VERSION tcl8
tar xfz tk$TCL8_VERSION-src.tar.gz
mv tk$TCL8_VERSION tk8

