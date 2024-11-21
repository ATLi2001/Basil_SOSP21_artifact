#!/bin/bash

N=10

for ((i=1; i<=N; i++)); do
    echo "Iteration: $i"
    ./server-tester.sh
    # lsof -ti:$((8000)) | xargs kill -9 &>/dev/null
    # store/server --config_path shard-r1.config --group_idx 0 --num_groups 1 --num_shards 1 --replica_idx 0 \
    #     --protocol sintr --debug_stats --num_client_hosts 2 --indicus_key_path keys \
    #     --data_file_path /usr/local/etc/tpcc-1-warehouse &> server0.out &
    sleep 1
    ./client-tester.sh
    # check client output
    if grep -q LATENCY client-0.out; then
        echo "client-0.out: OK"
    else
        exit 1
    fi
    if grep -q LATENCY client-1.out; then
        echo "client-1.out: OK"
    else
        exit 1
    fi
    # mv client-0.out client-0-$i.out
    # mv client-1.out client-1-$i.out
    # mv server0.out server0-$i.out
done

echo "Done"
