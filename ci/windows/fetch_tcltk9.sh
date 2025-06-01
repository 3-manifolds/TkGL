#!/usr/bin/bash
set -e
TCL9_VERSION=9.0.0
TK9_VERSION=9.0.0
# Find wget; on the ARM runners it's in the second location.
WGET="/msys64/usr/bin/wget"
if [ ! -e $WGET ]; then
    WGET="/usr/bin/wget"
fi
if [ ! -e tcl$TCL9_VERSION-src.tar.gz ] ; then
    $WGET -nv https://prdownloads.sourceforge.net/tcl/tcl$TCL9_VERSION-src.tar.gz
fi
if [ ! -e tk$TK9_VERSION-src.tar.gz ]; then
    $WGET -nv https://prdownloads.sourceforge.net/tcl/tk$TCL9_VERSION-src.tar.gz
fi
rm -rf tcl9 tk9
tar xfz tcl$TCL9_VERSION-src.tar.gz
mv tcl$TCL9_VERSION tcl9
tar xfz tk$TCL9_VERSION-src.tar.gz
mv tk$TCL9_VERSION tk9
