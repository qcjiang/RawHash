#!/bin/bash

uncalled pafstats -r ../true_mappings.paf --annotate ../uncalled/d1_sars-cov-2_r94_uncalled.paf > d1_sars-cov-2_r94_uncalled_ann.paf 2> d1_sars-cov-2_r94_uncalled.throughput
uncalled pafstats -r ../true_mappings.paf --annotate ../sigmap/d1_sars-cov-2_r94_sigmap.paf > d1_sars-cov-2_r94_sigmap_ann.paf 2> d1_sars-cov-2_r94_sigmap.throughput
uncalled pafstats -r ../true_mappings.paf --annotate ../rawhash/d1_sars-cov-2_r94_rawhash_viral.paf > d1_sars-cov-2_r94_rawhash_viral_ann.paf 2> d1_sars-cov-2_r94_rawhash_viral.throughput
uncalled pafstats -r ../true_mappings.paf --annotate ../rawhash/d1_sars-cov-2_r94_w3_rawhash_viral.paf > d1_sars-cov-2_r94_w3_rawhash_viral_ann.paf 2> d1_sars-cov-2_r94_w3_rawhash_viral.throughput
# uncalled pafstats -r ../true_mappings.paf --annotate ../rawhash/d1_sars-cov-2_r94_w5_rawhash_viral.paf > d1_sars-cov-2_r94_w5_rawhash_viral_ann.paf 2> d1_sars-cov-2_r94_w5_rawhash_viral.throughput


python ../../../../scripts/compare_pafs.py d1_sars-cov-2_r94_uncalled_ann.paf d1_sars-cov-2_r94_sigmap_ann.paf d1_sars-cov-2_r94_rawhash_viral_ann.paf > d1_sars-cov-2_r94_rawhash_viral.comparison
python ../../../../scripts/compare_pafs.py d1_sars-cov-2_r94_uncalled_ann.paf d1_sars-cov-2_r94_sigmap_ann.paf d1_sars-cov-2_r94_w3_rawhash_viral_ann.paf > d1_sars-cov-2_r94_w3_rawhash_viral.comparison
# python ../../../../scripts/compare_pafs.py d1_sars-cov-2_r94_uncalled_ann.paf d1_sars-cov-2_r94_sigmap_ann.paf d1_sars-cov-2_r94_w5_rawhash_viral_ann.paf > d1_sars-cov-2_r94_w5_rawhash_viral.comparison
