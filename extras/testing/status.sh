#!/bin/bash

HOSTS="dev0 dev1 dev2"

for HOST in $HOSTS; do 
	ssh $HOST "./erps/ringctl status" &
done

wait
