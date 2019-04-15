#!/bin/bash

echo "measurement	direction	size	min	avg	max	stddev	ign%"

for TEST in `ls measurements`; do
	for DIR in rx tx; do
		for LEN in `ls measurements/$TEST/$DIR | sort -n`; do
			# calculate average, without zeros
			AVG=$( grep 'main_thread.*[0-9]* pps' measurements/$TEST/$DIR/$LEN | awk '
			{
				if($4>0) {
					cnt++;
					total += $4;
				} else {
					ign++;
				}
			} END {
				printf("%d", cnt ? total/cnt : 0);
			}' )

			# compute minimum, maximum and standard deviation
			grep 'main_thread.*[0-9]* pps' measurements/$TEST/$DIR/$LEN | awk '
			BEGIN {
			    min = 9999999999;
				max = 0;
				stddev = 0;
				prev_zero = 1;
			}
			{
				if($4 > 0) {
					if (prev_zero) {
						prev_zero = 0;
						ign++;
					} else {
						cnt++;
						total += $4;
						total_dev += ($4 - '$AVG')^2;
						if ($4 > max) {
							max = $4;
						}
						if ($4 < min) {
							min = $4;
						}
					}
				} else {
					// ignore zero values
					prev_zero = 1;
					ign++;
				}
			} END {
				if (cnt > 0)
					stddev = sqrt(total_dev/cnt-1);
				else
					stddev = -1;

				printf("'$TEST'\t'$DIR'\t'$LEN'\t%d\t%d\t%d\t%d\t%.2f\n", 
					min,
					cnt ? total/cnt : 0, 
					max,
					stddev,
					(cnt+ign) ? ign/(cnt+ign) : 1.0);

			}'
		done
	done
done
