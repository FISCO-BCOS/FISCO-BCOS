#!/bin/sh
node init.js node0.sample node1.sample
chmod +x /bcos-data/node0/start0.sh
chmod +x /bcos-data/node1/start1.sh
sh /bcos-data/node0/start0.sh &
sh /bcos-data/node1/start1.sh &