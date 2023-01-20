#!/bin/bash

shuf -n521211 ../../rawhash/relative_abundance_rawhash_faster.paf > relative_abundance_rawhash_faster_25.paf
shuf -n208484 ../../rawhash/relative_abundance_rawhash_faster.paf > relative_abundance_rawhash_faster_10.paf
shuf -n20848 ../../rawhash/relative_abundance_rawhash_faster.paf > relative_abundance_rawhash_faster_1.paf
shuf -n2085 ../../rawhash/relative_abundance_rawhash_faster.paf > relative_abundance_rawhash_faster_01.paf
shuf -n209 ../../rawhash/relative_abundance_rawhash_faster.paf > relative_abundance_rawhash_faster_001.paf
shuf -n21 ../../rawhash/relative_abundance_rawhash_faster.paf > relative_abundance_rawhash_faster_001.paf

shuf -n521211 ../../uncalled/relative_abundance_uncalled.paf > relative_abundance_uncalled_25.paf
shuf -n208484 ../../uncalled/relative_abundance_uncalled.paf > relative_abundance_uncalled_10.paf
shuf -n20848 ../../uncalled/relative_abundance_uncalled.paf > relative_abundance_uncalled_1.paf
shuf -n2085 ../../uncalled/relative_abundance_uncalled.paf > relative_abundance_uncalled_01.paf
shuf -n209 ../../uncalled/relative_abundance_uncalled.paf > relative_abundance_uncalled_001.paf
shuf -n21 ../../uncalled/relative_abundance_uncalled.paf > relative_abundance_uncalled_001.paf

for i in `echo *.paf` ; do
if test -f $i; then
fname=`basename $i | sed 's/.paf/.abundance/g'`;
awk 'BEGIN{tot=0; covid=0; human=0; ecoli=0; yeast=0; algae=0; totsum=0; covidsum=0; humansum=0; ecolisum=0; yeastsum=0; algaesum=0;}{
	if(substr($6,1,5) == "ecoli" && $3 != "*" && $4 != "*" && $4 >= $3){ecoli++; count++; ecolisum+=$4-$3}
	else if(substr($6,1,5) == "yeast" && $3 != "*" && $4 != "*" && $4 >= $3){yeast++; count++; yeastsum+=$4-$3}
	else if(substr($6,1,11) == "green_algae" && $3 != "*" && $4 != "*" && $4 >= $3){algae++; count++; algaesum+=$4-$3}
	else if(substr($6,1,5) == "human" && $3 != "*" && $4 != "*" && $4 >= $3){human++; count++; humansum+=$4-$3}
	else if(substr($6,1,5) == "covid" && $3 != "*" && $4 != "*" && $4 >= $3){covid++; count++; covidsum+=$4-$3}
	}END{
		totsum = ecolisum + yeastsum + algaesum + humansum + covidsum;
		print "Ratio of bases: covid:" covidsum/totsum " ecoli: " ecolisum/totsum " yeast: " yeastsum/totsum " green_algae: " algaesum/totsum " human: " humansum/totsum;
	}' $i > $fname
fi done;
