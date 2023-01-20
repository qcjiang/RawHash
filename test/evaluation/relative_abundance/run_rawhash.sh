#!/bin/bash

THREAD=32

#relative_abundance
OUTDIR="./rawhash/"
FAST5="../../data/relative_abundance/fast5_files/"
REF="../../data/relative_abundance/ref.fa"
PORE="../../../extern/kmer_models/r9.4_180mv_450bps_6mer/template_median68pA.model"
PREFIX="relative_abundance"
mkdir -p ${OUTDIR}

#Faster preset
PRESET="faster"
bash ../../scripts/run_rawhash.sh ${OUTDIR} ${PREFIX} ${FAST5} ${REF} ${PORE} ${PRESET} ${THREAD} > "${OUTDIR}/${PREFIX}_rawhash_${PRESET}.out" 2> "${OUTDIR}/${PREFIX}_rawhash_${PRESET}.err"

#Fast preset
PRESET="fast"
bash ../../scripts/run_rawhash.sh ${OUTDIR} ${PREFIX} ${FAST5} ${REF} ${PORE} ${PRESET} ${THREAD} > "${OUTDIR}/${PREFIX}_rawhash_${PRESET}.out" 2> "${OUTDIR}/${PREFIX}_rawhash_${PRESET}.err"

#The following is the run using default parameters:
PRESET="sensitive"
bash ../../scripts/run_rawhash.sh ${OUTDIR} ${PREFIX} ${FAST5} ${REF} ${PORE} ${PRESET} ${THREAD} > "${OUTDIR}/${PREFIX}_rawhash_${PRESET}.out" 2> "${OUTDIR}/${PREFIX}_rawhash_${PRESET}.err"
