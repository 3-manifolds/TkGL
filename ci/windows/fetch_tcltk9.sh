#!/usr/bin/bash
set -e
TCL9_VERSION=9.0.0
TK9_VERSION=9.0.0
WGET="/c/msys64/usr/bin/wget -nv"
CMD=/c/msys64/usr/bin/cmd
if [ ! -e tcl$TCL9_VERSION-src.tar.gz ] ; then
    $WGET https://prdownloads.sourceforge.net/tcl/tcl$TCL9_VERSION-src.tar.gz
fi
if [ ! -e tk$TK9_VERSION-src.tar.gz ]; then
    $WGET https://prdownloads.sourceforge.net/tcl/tk$TCL9_VERSION-src.tar.gz
fi
rm -rf tcl9 tk9
tar xfz tcl$TCL9_VERSION-src.tar.gz
mv tcl$TCL9_VERSION tcl9
tar xfz tk$TCL9_VERSION-src.tar.gz
mv tk$TCL9_VERSION tk9
