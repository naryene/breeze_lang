!#/bin/bash

mkdir -p build && cmake -S . -B build
cmake --build build
build/breeze test.txt

