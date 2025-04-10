#!/bin/bash
USER=yh885

clean_up() {
processes=("retwisClient" "order" "storage" "client")

for p in ${processes[@]}; do
    cat init_servers_test.txt | awk '{print $1}' | xargs -P0 -I% ssh $USER@% killall -9 $p
done

cat init_servers_test.txt | awk '{print $1}' | xargs -P0 -I% ssh $USER@% sudo killall -9 retwisClient
cat init_servers_test.txt | awk '{print $1}' | xargs -P0 -I% ssh $USER@% sudo killall -9 client
}

clean_up
