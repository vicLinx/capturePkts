#!/bin/bash
#!/bin/sh

sudo ./build/capture-pkts -l 0-3 -n 4 -- -q 1 -p 0x3
