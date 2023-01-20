#!/bin/bash

echo "Uncalled throughput (mean/median)"
grep "BP per sec:" d1_sars-cov-2_r94_uncalled.throughput
echo "Sigmap throughput (mean/median)"
grep "BP per sec:" d1_sars-cov-2_r94_sigmap.throughput
echo "RawHash (Viral) throughput (mean/median)"
grep "BP per sec:" d1_sars-cov-2_r94_rawhash_viral.throughput
echo "RawHash (Sensitive) throughput (mean/median)"
grep "BP per sec:" d1_sars-cov-2_r94_rawhash_sensitive.throughput
echo "RawHash (Fast) throughput (mean/median)"
grep "BP per sec:" d1_sars-cov-2_r94_rawhash_fast.throughput
echo "RawHash (Faster) throughput (mean/median)"
grep "BP per sec:" d1_sars-cov-2_r94_rawhash_faster.throughput

echo;
grep "Uncalled Mean time per read" d1_sars-cov-2_r94_rawhash_faster.comparison
grep "Sigmap Mean time per read" d1_sars-cov-2_r94_rawhash_faster.comparison
echo "RawHash viral:"
grep "RawHash Mean time per read" d1_sars-cov-2_r94_rawhash_viral.comparison
echo "RawHash sensitive:"
grep "RawHash Mean time per read" d1_sars-cov-2_r94_rawhash_sensitive.comparison
echo "RawHash fast:"
grep "RawHash Mean time per read" d1_sars-cov-2_r94_rawhash_fast.comparison
echo "RawHash faster:"
grep "RawHash Mean time per read" d1_sars-cov-2_r94_rawhash_faster.comparison

echo;
echo '(Indexing) Timing and memory usage results:'
for i in `echo ../*/*index*.time`; do echo $i; 
	awk '{
		if(NR == 2){
			time = $NF
		}else if(NR == 3){
			time += $NF
			printf("CPU Time: %.2f\n", time)
		} else if(NR == 10){
			printf("Memory (GB): %.2f\n", $NF/1000000)
			print ""
		}
	}' $i; done

echo '(Mapping) Timing and memory usage results:'
for i in `echo ../*/*map.time`; do echo $i; 
	awk '{
		if(NR == 2){
			time = $NF
		}else if(NR == 3){
			time += $NF
			printf("CPU Time: %.2f\n", time)
		} else if(NR == 10){
			printf("Memory (GB): %.2f\n", $NF/1000000)
			print ""
		}
	}' $i; done
for i in `echo ../*/*_map_*.time`; do echo $i; 
	awk '{
		if(NR == 2){
			time = $NF
		}else if(NR == 3){
			time += $NF
			printf("CPU Time: %.2f\n", time)
		} else if(NR == 10){
			printf("Memory (GB): %.2f\n", $NF/1000000)
			print ""
		}
	}' $i; done

echo;
grep "Uncalled precision:" d1_sars-cov-2_r94_rawhash_faster.comparison
grep "Sigmap precision:" d1_sars-cov-2_r94_rawhash_faster.comparison
echo "RawHash viral:"
grep "RawHash precision:" d1_sars-cov-2_r94_rawhash_viral.comparison
echo "RawHash sensitive:"
grep "RawHash precision:" d1_sars-cov-2_r94_rawhash_sensitive.comparison
echo "RawHash fast:"
grep "RawHash precision:" d1_sars-cov-2_r94_rawhash_fast.comparison
echo "RawHash faster:"
grep "RawHash precision:" d1_sars-cov-2_r94_rawhash_faster.comparison

echo;
grep "Uncalled recall:" d1_sars-cov-2_r94_rawhash_faster.comparison
grep "Sigmap recall:" d1_sars-cov-2_r94_rawhash_faster.comparison
echo "RawHash viral:"
grep "RawHash recall:" d1_sars-cov-2_r94_rawhash_viral.comparison
echo "RawHash sensitive:"
grep "RawHash recall:" d1_sars-cov-2_r94_rawhash_sensitive.comparison
echo "RawHash fast:"
grep "RawHash recall:" d1_sars-cov-2_r94_rawhash_fast.comparison
echo "RawHash faster:"
grep "RawHash recall:" d1_sars-cov-2_r94_rawhash_faster.comparison

echo;
grep "Uncalled F-1 score:" d1_sars-cov-2_r94_rawhash_faster.comparison
grep "Sigmap F-1 score:" d1_sars-cov-2_r94_rawhash_faster.comparison
echo "RawHash viral:"
grep "RawHash F-1 score:" d1_sars-cov-2_r94_rawhash_viral.comparison
echo "RawHash sensitive:"
grep "RawHash F-1 score:" d1_sars-cov-2_r94_rawhash_sensitive.comparison
echo "RawHash fast:"
grep "RawHash F-1 score:" d1_sars-cov-2_r94_rawhash_fast.comparison
echo "RawHash faster:"
grep "RawHash F-1 score:" d1_sars-cov-2_r94_rawhash_faster.comparison
