#!/bin/sh

# Загрузка Boost.Log
echo "Downloading Boost..."
cd libs || exit 1
wget https://boostorg.jfrog.io/artifactory/main/release/1.78.0/source/boost_1_78_0.tar.bz2

# Разархивирование Boost
echo "Extracting Boost..."
tar --bzip2 -xf boost_1_78_0.tar.bz2
rm boost_1_78_0.tar.bz2

# Сборка Boost.Log
echo "Building Boost.Log..."
cd boost_1_78_0 || exit 1
sudo ./bootstrap.sh
sudo ./b2 install --with-log
cd ../

# Загрузка Sentry
echo "Building Sentry..."
git clone --recurse-submodules https://github.com/getsentry/sentry-native.git

# Сборка ядра
echo "Building Core..."
mkdir -p build/Debug
cd build/Debug || exit 1
cmake ../..
cmake --build . --clean-first
