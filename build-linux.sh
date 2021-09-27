#!/usr/bin/env bash
# Copyright 2021 Dokyung Song. All rights reserved.
# Use of this source code is governed by Apache 2 LICENSE that can be found in the LICENSE file.


CONFIGS=(`basename configs/*defconfig` tinyconfig)

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
	echo "Usage: $0 <KERNEL_SRC_DIR> <CONFIG>" >&2
	echo "       CONFIG: ${CONFIGS[*]}" >&2
	exit 1
fi

KERNEL_SRC_DIR=$1
defconfig=csi2115_f21_defconfig

if [ ! -d $KERNEL_SRC_DIR ]; then
	echo $KERNEL_SRC_DIR does not exist.
	exit 1
fi

if [ $# -eq 2 ]; then
	defconfig=$2
fi

BUILD_DIR=$(dirname $0)/build
mkdir -p $BUILD_DIR

IMAGE_OUT_DIR=$BUILD_DIR/linux

IMAGE_OUT_DIR=$PWD/$IMAGE_OUT_DIR

if [ ! -d $IMAGE_OUT_DIR ]; then
	echo Creating $IMAGE_OUT_DIR
	mkdir -p $IMAGE_OUT_DIR
fi

INSTALL_MOD_PATH=$IMAGE_OUT_DIR/modules

if [ $defconfig = "tinyconfig" ]; then
	INSTALL_MOD_PATH=""
else
	cp configs/${defconfig} $KERNEL_SRC_DIR/arch/x86/configs
fi

pushd $KERNEL_SRC_DIR

set -eux

export CC=gcc-8
export CXX=g++-8

make ${defconfig} O=$IMAGE_OUT_DIR/csi2115_f21
make -j8 O=$IMAGE_OUT_DIR/csi2115_f21 CC=$CC

if [ "$INSTALL_MOD_PATH" != "" ]; then
	pushd $IMAGE_OUT_DIR/csi2115_f21
	make modules_install INSTALL_MOD_PATH=$INSTALL_MOD_PATH
	popd
fi

popd
