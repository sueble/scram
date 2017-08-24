#!/usr/bin/env bash

set -ev

# Boost
sudo apt-get install \
  libboost-{program-options,math,random,filesystem,system,date-time}1.58-dev
