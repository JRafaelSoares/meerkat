#!/bin/sh

BASEDIR=$(dirname "$0")
USER=`whoami`
TARGET_PATH=/home/$USER/zipkat/
CODEBASE=$BASEDIR/..
EXEC_PATH=/home/$USER/workspace/zipkat/
ZIPLOG_EXEC_PATH=/home/$USER/workspace/ziplog/build

#cat init_servers.txt|xargs -P0 -I% rsync -az install.sh build.sh clean.sh go $USER@%:
#cat init_servers.txt|xargs -I% ssh $USER@% sh install.sh
#cat init_servers.txt|xargs -I% ssh $USER@% sh clean.sh
#cat init_servers.txt|xargs -I% rsync -az $CODEBASE $USER@%:${TARGET_PATH}
#cat init_servers.txt|xargs -I% rsync -az $USER@%:ziplog .
#cat init_servers.txt|xargs -P0 -I% rsync -az ${EXEC_PATH}/store/meerkatstore/meerkatir/meerkat_server $USER@%:${TARGET_PATH}
cat init_servers.txt|xargs -P0 -I% rsync -az ${EXEC_PATH}/store/benchmark/* $USER@%:${TARGET_PATH}
cat init_servers.txt|xargs -P0 -I% rsync -az ${EXEC_PATH}/*.shard0.config $USER@%:${TARGET_PATH}
cat init_servers.txt|xargs -P0 -I% rsync -az ${ZIPLOG_EXEC_PATH}/* $USER@%:${TARGET_PATH}
#cat init_servers.txt|xargs -P0 -I% rsync -az ${EXEC_PATH}/store/tools/f1.shard0.config $USER@%:${TARGET_PATH}
#cat init_servers.txt|xargs -P0 -I% rsync -az ${EXEC_PATH}/store/tools/keys $USER@%:${TARGET_PATH}
#cat init_servers.txt|xargs -P0 -I% rsync -az $CODEBASE/fractus/configs $USER@%:${TARGET_PATH}
#cat init_servers.txt|xargs -P0 -I% rsync -az $CODEBASE/dep-setup.sh $USER@%:${TARGET_PATH}
#cat init_servers.txt|xargs -I% ssh $USER@% sh ${TARGET_PATH}/dep-setup.sh
#cat init_servers.txt|xargs -P0 -I% rsync -az  $CODEBASE/cpp_impl/build/release/bin/zipstore-server $USER@%:${TARGET_PATH}
#cat init_servers.txt|xargs -P0 -I% rsync -az  $CODEBASE/cpp_impl/build/release/bin/zipstore-client $USER@%:${TARGET_PATH}
#cat init_servers.txt|xargs -P0 -I% rsync -az  $CODEBASE/fractus/ping_pong $USER@%:${TARGET_PATH}
#cat init_servers.txt|xargs -P0 -I% rsync -az  $CODEBASE/fractus/workloads $USER@%:${TARGET_PATH}
#rm ziplog

# setup
#cat init_servers.txt | awk '{print $1}' | xargs -P0 -I% ssh $USER@% sudo sysctl -w vm.nr_hugepages=1024
cat init_servers.txt | awk '{print $1}' | xargs -P0 -I% ssh $USER@% sudo mkdir -p /mnt/log
cat init_servers.txt | awk '{print $1}' | xargs -P0 -I% ssh $USER@% sudo chmod 777 /mnt/log
#cat init_servers.txt | awk '{print $1}' | xargs -n1 -P0 -I% ssh $USER@% 'sudo sysctl kernel.sched_min_granularity_ns=3000000; sudo sysctl kernel.sched_latency_ns=3000000'
#cat init_servers.txt | awk '{print $1}' | xargs -n1 -P0 -I% ssh $USER@% sudo timedatectl set-ntp true
#cat init_servers.txt | awk '{print $1}' | xargs -P0 -I% ssh $USER@% sudo apt install -y libevent-dev 
