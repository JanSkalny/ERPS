#!/usr/local/bin/bash

MODULE=erps.ko

for HOST in test0 ; do
	echo "deploying on $HOST"
	rsync -a ~/erps/ $HOST:erps/
	ssh $HOST "cd erps/ && make -DWITH_NETMAP_HEAD=1 unload clean all install load; sync" > /dev/null
done
