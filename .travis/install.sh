#!/usr/bin/env bash

set -ev

# CMake 3.5
sudo apt-get install cmake3

# Boost
sudo apt-get install \
  libboost-{program-options,math,random,filesystem,system,date-time}1.58-dev
