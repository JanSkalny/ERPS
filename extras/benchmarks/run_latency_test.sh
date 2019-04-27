#!/bin/bash

PONG=gen
PONG_DEV=em1
PONG_MAC="00:15:17:b2:03:47"

PING=gen
PING_DEV=em0
PING_MAC="00:15:17:b2:03:46"

DURATION=105
WAIT=5
DURATION=$( expr $DURATION - $WAIT )

PING_ARGS="-D $PONG_MAC -S $PING_MAC"
PONG_ARGS="-S $PONG_MAC -D $PING_MAC"
#PING_ARGS="-S 00:1b:21:13:db:d4 -D AC:22:0B:8D:4C:8A -s 1.1.1.10 -d 2.2.2.20"
COMMON_ARGS="-w $WAIT -N -A"
PG="/usr/obj/usr/src/amd64.amd64/tools/tools/netmap/pkt-gen"
#PG="pkt-gen"

function run_test {
	TEST=$1
	PING_ARGS2=""
	if [ $# -eq 2 ]; then 
		PING_ARGS2="-R $2pps -n $(( $DURATION * $2 ))"
		OUT="$2pps"
	else
		PING_ARGS2="-n $(( $DURATION * 1000 ))"
		OUT="unlimited"
	fi

	# cleanup results from previous test
	rm measurements/$TEST/pong/$OUT measurements/$TEST/ping/$OUT >/dev/null 2>&1

	# kill previous test (if any)
	ssh $PING killall pkt-gen > /dev/null 2>&1
	#[[ "$PING" != "$PONG" ]] && ssh $PONG killall pkt-gen > /dev/null 2>&1
	sleep 4

	# start measurement 
	echo "starting sink (pong)"
	ssh $PONG "$PG -i $PONG_DEV -f pong $PONG_ARGS $COMMON_ARGS" > measurements/$TEST/pong/$OUT 2>&1 &
	PONG_PID=$!
	sleep $WAIT

	# start generator 
	echo "starting generator"
	ssh $PING "$PG -i $PING_DEV -f ping $PING_ARGS $PING_ARGS2 $COMMON_ARGS" > measurements/$TEST/ping/$OUT 2>&1 &
	PING_PID=$!
	sleep $WAIT

	# testing in progress
	echo "test is running... (t=$DURATION rate=$OUT)"
	sleep $DURATION

	# stop testing
	echo "stopping test..."
	sleep 20
	ssh $PING killall pkt-gen &
	#[[ "$PING" != "$PONG" ]] && ( ssh $PONG killall pkt-gen & )

	wait $PONG_PID $PING_PID
	echo "done"
	sleep $WAIT
}

if [ $# -eq 0 ]; then
	echo "usage: ./run_latency_test.sh TEST_NAME (LEN)" 1>&2
	exit 1
fi

TEST=$1
mkdir -p measurements/$TEST/ping measurements/$TEST/pong > /dev/null 2>&1

if [ $# -eq 2 ]; then
	# run one specific test
	run_test $TEST $2
else
	# run tests at different pps
	#run_test $TEST 100
	run_test $TEST 500
	#run_test $TEST 1000
	run_test $TEST 4000
	run_test $TEST 8000
	run_test $TEST
fi
