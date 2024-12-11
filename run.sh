#!/bin/bash

echo "Starting Experiment"

mkdir -p results/basil/
mkdir -p results/sintr/
cd src/

echo "Running Basil"
./server-tester.sh indicus
sleep 1
./client-tester.sh indicus
mv client-*.out ../results/basil/
mv stats-*.json ../results/basil/

echo "Running Sintr"
./server-tester.sh sintr
sleep 1
./client-tester.sh sintr
mv client-*.out ../results/sintr/
mv stats-*.json ../results/sintr/

echo "Experiment Complete"
