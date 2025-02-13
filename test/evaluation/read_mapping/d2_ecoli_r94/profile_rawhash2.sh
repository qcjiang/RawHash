#!/bin/bash

#Please make sure that you compile rawhash2 with the profiling option enabled (see README.md)

THREAD=$1

#d2_ecoli_r94
OUTDIR="./rawhash2/"
FAST5="../../../data/d2_ecoli_r94/fast5_files/barcode02_r0barcode02b0_0.fast5"
REF="../../../data/d2_ecoli_r94/ref.fa"
PORE="../../../../extern/kmer_models/legacy/legacy_r9.4_180mv_450bps_6mer/template_median68pA.model"
PREFIX="d2_ecoli_r94_profile_"${THREAD}
mkdir -p ${OUTDIR}

#The following is the run using default parameters:
PRESET="sensitive"
bash ../../../scripts/run_rawhash2.sh ${OUTDIR} ${PREFIX} ${FAST5} ${REF} ${PORE} ${PRESET} ${THREAD} > "${OUTDIR}/${PREFIX}_rawhash2_${PRESET}.out" 2> "${OUTDIR}/${PREFIX}_rawhash2_${PRESET}.err"
