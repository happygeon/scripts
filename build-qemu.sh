#!/usr/bin/env bash
# Copyright 2021 Dokyung Song. All rights reserved.
# Use of this source code is governed by Apache 2 LICENSE that can be found in the LICENSE file.

if [ $# -lt 1 ] || [ $# -gt 1 ]; then
	echo "Usage: $0 <QEMU_SRC_DIR>" >&2
	exit 1
fi

QEMU_SRC_DIR=$1

if [ ! -d $QEMU_SRC_DIR ]; then
	echo $QEMU_SRC_DIR does not exist.
	exit 1
fi

QEMU_SRC_DIR=`realpath $QEMU_SRC_DIR`

BUILD_DIR=$(dirname $0)/build/qemu
mkdir -p $BUILD_DIR

BUILD_DIR=`realpath $BUILD_DIR`
INSTALL_DIR=$BUILD_DIR/install

mkdir -p $INSTALL_DIR

pushd $BUILD_DIR

set -eux

$QEMU_SRC_DIR/configure --prefix=$INSTALL_DIR --target-list=x86_64-softmmu --disable-werror
make -j4
make install

popd
