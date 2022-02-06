#!/bin/sh

# Сборка Boost.Log
echo "Building Boost.Log..."
cd libs/boost_1_78_0 || exit 1
sudo ./bootstrap.sh
sudo ./b2 install --with-log
cd ../..

# Сборка ядра
echo "Building Core..."
mkdir -p build/Debug
cd build/Debug || exit 1
cmake ../..
cmake --build . --clean-first
