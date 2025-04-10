#!/bin/bash
USER=yh885

run() {

cat init_servers_test.txt | awk '{print $1}' | xargs -P0 -I% ssh $USER@% 'hostname > /home/yh885/t; echo $EPOCHREALTIME >> /home/yh885/t; cat /home/yh885/t; rm /home/yh885/t'
#cat servers.txt | awk '{print $1}' | xargs -P0 -I% ssh $USER@% 'sudo sysctl -w kernel.sched_min_granularity_ns=1000000; sudo sysctl -w kernel.sched_latency_ns=1000000'
}

run
