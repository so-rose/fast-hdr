#!/bin/bash

set -e

g++ --std=c++17 -o hdr_sdr_gen -fopenmp -O3 -march=native hdr_sdr_gen.cpp
