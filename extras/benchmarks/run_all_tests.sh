#!/bin/bash

function fail() {
	echo "$*" 1>&2
	exit 1
}

PLATFORM="lanner"
SEQS="1 2 3"
# make sure we will reboot into linux
ansible-playbook playbooks/boot-linux.yml

# run linux tests first
for TEST in linux-bridge linux-ovs-apt; do
	for X in $SEQS; do
		ansible-playbook playbooks/deploy/$TEST.yml || fail "failed to deploy dest $TEST"
		sleep 10
		./run_test.sh $PLATFORM-$TEST-$X || sleep 60
		sleep 10
	done
done

# reboot into freebsd
ansible-playbook playbook/boot-freebsd.yml

# run freebsd tests
for TEST in freebsd-bridge freebsd-pcap-select freebsd-pcap-ev freebsd-vale; do
	for X in $SEQS; do
		ansible-playbook playbooks/deploy/$TEST.yml || fail "failed to deploy dest $TEST"
		sleep 10
		./run_test.sh $PLATFORM-$TEST-$X || sleep 60
		sleep 10
	done
done

# reboot into linux (manually) and run boot-linux
