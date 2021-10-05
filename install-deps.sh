#!/usr/bin/env bash
# Copyright 2021 Dokyung Song. All rights reserved.
# Use of this source code is governed by Apache 2 LICENSE that can be found in the LICENSE file.

sudo apt update
sudo apt install gcc-8
sudo apt install flex bison make git
sudo apt install libssl-dev libelf-dev
sudo apt install debootstrap
sudo apt install python
sudo apt install ninja-build
sudo apt install automake
sudo apt install cmake
sudo apt install libglib2.0-dev libpixman-1-dev
sudo apt install cpu-checker
sudo dpkg -i dwarves_1.17-1_amd64.deb
