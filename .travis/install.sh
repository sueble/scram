#!/usr/bin/env bash

set -ev

# Installing dependencies from source.
PROJECT_DIR=$PWD
cd /tmp

# Boost
wget https://sourceforge.net/projects/iscram/files/deps/boost-scram.tar.bzip2
tar -xf ./boost-scram.tar.bzip2  # Sets up install dir.
sudo mv ./install/lib/* /usr/lib/
sudo mv ./install/include/boost /usr/include/

# Libxml++
LIBXMLPP='libxml++-2.38.1'
wget http://ftp.gnome.org/pub/GNOME/sources/libxml++/2.38/${LIBXMLPP}.tar.xz
tar -xf ${LIBXMLPP}.tar.xz
(cd ${LIBXMLPP} && ./configure && make -j 2 && sudo make install)

cd $PROJECT_DIR
