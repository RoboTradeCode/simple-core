#!/bin/sh

# Сборка ядра
echo "Building Core..."
mkdir -p build/Debug
cd build/Debug || exit 1
cmake ../..
cmake --build .
