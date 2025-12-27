#!/bin/bash
# Final Test File made with gen AI
set -e

PORT=5678

killall sensor_gateway sensor_node 2>/dev/null || true
echo "=== Building project ==="
make clean
make all

echo
echo "======================================"
echo " Test 1: 3 sensors (like test3.sh)   "
echo "======================================"
clients=3
echo "starting gateway"
./sensor_gateway $PORT $clients &
GW_PID=$!
sleep 3

echo "starting 3 sensor nodes"
./sensor_node 15 1 127.0.0.1 $PORT &
sleep 2
./sensor_node 21 3 127.0.0.1 $PORT &
sleep 2
./sensor_node 37 2 127.0.0.1 $PORT &
sleep 11

echo "killing all sensor_node processes (Test 1)"
killall sensor_node 2>/dev/null || true
sleep 5
echo "killing sensor_gateway (Test 1)"
killall sensor_gateway 2>/dev/null || true
sleep 3

[ -f data.csv ] && mv -f data.csv data_test1.csv
[ -f gateway.log ] && mv -f gateway.log gateway_test1.log


echo
echo "======================================"
echo " Test 2: 5 sensors (like test5.sh)   "
echo "======================================"
clients=5
echo "starting gateway"
./sensor_gateway $PORT $clients &
GW_PID=$!
sleep 3

echo "starting 5 sensor nodes"
./sensor_node 15 1 127.0.0.1 $PORT &
sleep 1
./sensor_node 21 1 127.0.0.1 $PORT &
sleep 1
./sensor_node 37 1 127.0.0.1 $PORT &
sleep 1
./sensor_node 132 1 127.0.0.1 $PORT &
sleep 1
./sensor_node 142 1 127.0.0.1 $PORT &
sleep 11

echo "killing all sensor_node processes (Test 2)"
killall sensor_node 2>/dev/null || true
sleep 5
echo "killing sensor_gateway (Test 2)"
killall sensor_gateway 2>/dev/null || true
sleep 3

[ -f data.csv ] && mv -f data.csv data_test2.csv
[ -f gateway.log ] && mv -f gateway.log gateway_test2.log


echo
echo "======================================================"
echo " Test 3: timeout + max_conn behavior (101/202/303/404)"
echo "======================================================"
clients=2
echo "starting gateway"
./sensor_gateway $PORT $clients &
GW_PID=$!
sleep 3

echo "starting sensor 101 (1s), 202 (1s), 303 (7s)"
./sensor_node 101 1 127.0.0.1 $PORT &
sleep 1
./sensor_node 202 1 127.0.0.1 $PORT &
sleep 1
./sensor_node 303 7 127.0.0.1 $PORT &
# let 303 time out, 101 & 202 run a bit
sleep 20

echo "attempting to start sensor 404 (should be blocked/refused if max_conn reached)"
./sensor_node 404 1 127.0.0.1 $PORT &
sleep 10

echo "killing all sensor_node processes (Test 3)"
killall sensor_node 2>/dev/null || true
sleep 5
echo "killing sensor_gateway (Test 3)"
killall sensor_gateway 2>/dev/null || true
sleep 3

[ -f data.csv ] && mv -f data.csv data_test3.csv
[ -f gateway.log ] && mv -f gateway.log gateway_test3.log

echo
echo "All tests done."
echo "Check:"
echo "  - data_test1.csv, gateway_test1.log"
echo "  - data_test2.csv, gateway_test2.log"
echo "  - data_test3.csv, gateway_test3.log"
