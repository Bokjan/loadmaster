#!/bin/bash

if [ $1 == "-h" ] || [ $1 == "--help" ] ; then
echo "USAGE: $0 [binary_name] [build_dir]"
exit 0;
fi

binary_name="loadmaster"
build_dir="build_rel"

if [ $# -ge 1 ] ; then
    binary_name=$1
fi
if [ $# -ge 2 ] ; then
    build_dir=$2
fi

mkdir -p $build_dir && cd $build_dir
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j
cd ..
cp $build_dir/src/loadmaster ./$binary_name
strip $binary_name
file $binary_name
