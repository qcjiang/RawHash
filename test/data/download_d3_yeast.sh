#!/bin/bash

mkdir d3_yeast_r94
cd d3_yeast_r94

#Download FAST5 from AWS. NCBI SRA Accession: https://trace.ncbi.nlm.nih.gov/Traces/index.html?view=run_browser&acc=SRR8648503&display=metadata
wget -qO-  https://sra-pub-sars-cov2.s3.amazonaws.com/sra-src/SRR8648503/GLU1II_basecalled_fast5_1.tar.gz.1 | tar -xzv;

mkdir -p ./fast5_files/; find ./GLU1II_basecalled_fast5_1 -type f -name '*.fast5' | head -50000 | xargs -i{} mv {} ./fast5_files/; rm -rf GLU1II_basecalled_fast5_1;

#To extract the reads from FAST5 from this dataset, you will need to clone the following repository and make sure you have h5py <= 2.9 (if you have conda you can do the following):
# conda create -n oldh5 h5py=2.9.0; conda activate oldh5;
# git clone https://github.com/rrwick/Fast5-to-Fastq
# Fast5-to-Fastq/fast5_to_fastq.py fast5_files/ | awk 'BEGIN{line = 0}{line++; if(line %4 == 1){print ">"substr($1,2,36)}else if(line % 4 == 2){print $0}}' > reads.fasta

#We will provide the direct Zenodo link to this reads.fasta file as well to save from the hassle

#Downloading S.cerevisiae S288c (Yeast) reference genome from UCSC
wget https://hgdownload.soe.ucsc.edu/goldenPath/sacCer3/bigZips/sacCer3.fa.gz; gunzip sacCer3.fa.gz; mv sacCer3.fa ref.fa; 

cd ..
