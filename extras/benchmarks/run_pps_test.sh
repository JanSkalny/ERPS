#!/bin/bash

SINK=gen
SINK_DEV=em1

GEN=gen
GEN_DEV=em0

DURATION=105
WAIT=5
DURATION=$( expr $DURATION - $WAIT )

GEN_ARGS="-S 10:10:10:10:10:10"
#GEN_ARGS="-S 00:1b:21:13:db:d4 -D AC:22:0B:8D:4C:8A -s 1.1.1.10 -d 2.2.2.20"
COMMON_ARGS="-w $WAIT -T 1000 -N -A"
PG="/usr/obj/usr/src/amd64.amd64/tools/tools/netmap/pkt-gen"
#PG="pkt-gen"

function run_test {
	TEST=$1
	LEN=$2

	# kill previous test (if any)
	ssh $GEN killall pkt-gen > /dev/null 2>&1
	#[[ "$GEN" != "$SINK" ]] && ssh $SINK killall pkt-gen > /dev/null 2>&1
	sleep 4

	PLEN=$( expr $LEN - 4)

	# start measurement 
	echo "starting sink"
	ssh $SINK "$PG -i $SINK_DEV -f rx $COMMON_ARGS" > measurements/$TEST/rx/$LEN 2>&1 &
	SINK_PID=$!
	sleep $WAIT

	# start generator 
	echo "starting generator ($LEN Bytes)"
	ssh $GEN "$PG -i $GEN_DEV -f tx $GEN_ARGS -l $PLEN $COMMON_ARGS" > measurements/$TEST/tx/$LEN 2>&1 &
	GEN_PID=$!
	sleep $WAIT

	# testing in progress
	echo "test is running... (t=$DURATION)"
	sleep $DURATION

	# stop testing
	echo "stopping test..."
	ssh $GEN killall pkt-gen &
	#[[ "$GEN" != "$SINK" ]] && ( ssh $SINK killall pkt-gen & )

	wait $SINK_PID $GEN_PID
	echo "done"
	sleep $WAIT
}

if [ $# -eq 0 ]; then
	echo "usage: ./run_pps_test.sh TEST_NAME (LEN)" 1>&2
	exit 1
fi

TEST=$1
mkdir -p measurements/$TEST/tx measurements/$TEST/rx > /dev/null 2>&1


if [ $# -eq 2 ]; then
	# run test for specific length
	echo "removing old measurements..."
	LEN=$2
	rm measurements/$TEST/tx/$LEN measurements/$TEST/rx/$LEN > /dev/null 2>&1
	run_test $TEST $LEN
else
	# run tests for all valid lengths
	echo "removing old measurements..."
	rm measurements/$TEST/tx/* measurements/$TEST/rx/* > /dev/null 2>&1
	for LEN in $( seq 64 64 1500 ) 1500; do # 1518
		run_test $TEST $LEN
	done
fi

