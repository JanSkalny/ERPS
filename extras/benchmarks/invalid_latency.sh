#!/bin/bash

function fail() {
	echo "$*" 1>&2
	exit 1
}

[ $# -ne 1 ] && fail "usage: ./invalid_latency.sh {show|test}"
RUN_TESTS=0
[ $1 == "test" ] && RUN_TESTS=1
[ $1 == "show" ] && RUN_TESTS=0

for RESULT in `ls measurements/*/ping/*`; do
	TEST=$( echo $RESULT | cut -d '/' -f 2 )
	RATE=$( echo $RESULT | cut -d '/' -f 4 | sed 's/pps//' )
	RTT_COUNT=$( grep RTT_ $RESULT  | wc -l  | awk '{print $1}' )
	REAL_RATE=$( grep 'count [0-9][0-9]' $RESULT | head -n1 | awk '{print $5}' )
	CNT=$( grep seq $RESULT | wc -l | awk '{print $1}')

	DEPLOY=$( echo $TEST | sed 's/^lanner-//' | sed 's/-[0-9]*$//' )

	if [ $RTT_COUNT -lt 5 ]; then
		echo "test=$TEST rate=$RATE: invalid test! got only $RTT_COUNT seconds of data"
		if [ $RUN_TESTS -eq 1 ]; then 
			ansible-playbook playbooks/deploy/$DEPLOY.yml || fail "ansible failed"
			./run_latency_test.sh $TEST $RATE
		fi
		continue
	fi

	if [ $CNT -lt 100 ]; then
		echo "test=$TEST rate=$RATE: invalid test! got only $CNT individual results"
		if [ $RUN_TESTS -eq 1 ]; then 
			ansible-playbook playbooks/deploy/$DEPLOY.yml || fail "ansible failed"
			./run_latency_test.sh $TEST $RATE
		fi
		continue
	fi

	if [ $RATE != "unlimited" ]; then
		if [ $REAL_RATE -lt $( expr $RATE - 5 ) ]; then
			echo "test=$TEST rate=$RATE: rate too low! real_rate=$REAL_RATE"
		fi
	fi

done
