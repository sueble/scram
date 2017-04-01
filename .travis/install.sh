#!/usr/bin/env bash

set -ev

# Boost
sudo apt-get install \
  libboost-{program-options,math,random,filesystem,system,date-time}1.58-dev

# Installing dependencies from source.
PROJECT_DIR=$PWD
cd /tmp

# Libxml++
LIBXMLPP='libxml++-2.38.1'
wget http://ftp.gnome.org/pub/GNOME/sources/libxml++/2.38/${LIBXMLPP}.tar.xz
tar -xf ${LIBXMLPP}.tar.xz
(cd ${LIBXMLPP} && ./configure && make -j 2 && sudo make install)

cd $PROJECT_DIR
