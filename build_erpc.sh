#!/bin/bash

cd third_party/eRPC/
rm -rf build
mkdir build
cd build
cmake .. -DPERF=OFF -DTRANSPORT=infiniband -DROCE=on; make -j
