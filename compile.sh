#!/bin/bash

set -e

rm -r dist
mkdir -p dist

# Compile to Dist
g++ --std=c++17 -pthread -o dist/hdr_sdr -fopenmp -fopenmp-simd -Ofast -frename-registers -funroll-loops -D_GLIBCXX_PARALLEL -flto src/hdr_sdr.cpp

# Generate LUT8
cd gen
./compile.sh
./hdr_sdr_gen ../dist/cnv.lut8
cd -

# Copy Things to Dist
#~ cp src/hdr_sdr.cpp dist/
cp src/ffmpeg dist/

cp res/* dist/
