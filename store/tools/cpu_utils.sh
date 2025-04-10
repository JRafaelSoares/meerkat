#!/bin/bash
USER=yh885

run() {

cat init_servers.txt | awk '{print $1}' | xargs -P0 -I% ssh $USER@% sudo mpstat 2
#cat servers.txt | awk '{print $1}' | xargs -P0 -I% ssh $USER@% 'sudo sysctl -w kernel.sched_min_granularity_ns=1000000; sudo sysctl -w kernel.sched_latency_ns=1000000'
}

run
