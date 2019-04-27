#!/bin/bash

function fail() {
	echo "$*" 1>&2
	exit 1
}

echo "processing..."
./process.sh | grep linux | grep apt | \
while read LINE; do
	INVAL=0
	STDDEV=$( echo "$LINE" | awk '{print $7}')
	MIN=$( echo "$LINE" | awk '{print $4}')
	TEST=$( echo "$LINE" | awk '{print $1}')
	LEN=$( echo "$LINE" | awk '{print $3}')
	SAMPLES=$( echo "$LINE" | awk '{print $9}')

	DEPLOY=$( echo $TEST | sed 's/^lanner-//' | sed 's/-[0-9]*$//' )

	[[ $STDDEV -ge 10000 ]] && INVAL=1
	[[ $MIN -le 10000 ]] && INVAL=1
	[[ $SAMPLES -le 90 ]] && INVAL=1
	[[ $SAMPLES -ge 105 ]] && INVAL=1
	( echo "$LINE" | grep 9999999999 >/dev/null ) && INVAL=1

	[[ $INVAL -eq 1 ]] || continue
   
	#echo "inval $DEPLOY $TEST $LEN"
	echo "retest invalid measurement $TEST $LEN $LINE"

	#ansible-playbook playbooks/deploy/$DEPLOY.yml || fail "ansible failed"
	#./run_test.sh $TEST $LEN
done
