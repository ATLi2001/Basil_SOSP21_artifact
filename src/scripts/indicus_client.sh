store/benchmark/async/benchmark --config_path shard-r0.config --num_groups 1 --num_shards 1 --protocol_mode indicus --num_keys 1 --benchmark rw --num_ops_txn 2 --exp_duration 10 --client_id 0 --warmup_secs 0 --cooldown_secs 0 --key_selector zipf --zipf_coefficient 0.0 --stats_file "stats-0.json" --indicus_key_path keys