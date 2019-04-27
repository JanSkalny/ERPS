#!/bin/bash

echo "TEST RATE REAL_RATE MIN AVG MAX STDDEV CNT DURATION" | sed 's/ /	/g'
for RESULT in `ls measurements/*/ping/*`; do
	TEST=$( echo $RESULT | cut -d '/' -f 2 )
	RATE=$( echo $RESULT | cut -d '/' -f 4 | sed 's/pps//' )
	REAL_RATE=$( grep 'count [0-9][0-9]' $RESULT | head -n1 | awk '{print $5}' )
	RTT_COUNT=$( grep RTT_ $RESULT  | wc -l  | awk '{print $1}' )
	RES=$( grep RTT_ $RESULT | tail -n 1 | awk '{print $8}' | sed 's/\// /g' )
	CNT=$( grep seq $RESULT | wc -l | awk '{print $1}')

	if [ "x$RES" == "x" ]; then
		RES="- - - -"
	fi

	echo "$TEST $RATE $REAL_RATE $RES $CNT $RTT_COUNT" | sed 's/ /	/g' | sed 's/\./,/g' 
done
