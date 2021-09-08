### Introduction

This is a simple network packet analyzer based on DPDK l2fwd example.

### DPDK Version

- DPDK 17.08

### How to run it

1. Install requirements

2. Build and Setup DPDK library, including

   - Correctly set environment variable `RTE_SDK` and `RTE_TARGET` e.g. bashrc
   - Insert `IGB UIO` module, setup hugpage, bind Ethernet device to `IGB UIO` module

3. Build and Run the application

   `make`

   `sudo ./build/capture-pkts -l 0-3 -n 4 -- -q 1 -p 0x3`