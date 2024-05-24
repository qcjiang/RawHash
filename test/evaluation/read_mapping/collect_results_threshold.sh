#!/bin/bash

OUTPUT_CSV="comparison_results.csv"

folders=(
    "d1_sars-cov-2_r94"
    "d2_ecoli_r94"
    "d3_yeast_r94"
    "d4_green_algae_r94"
    "d5_human_na12878_r94"
    "d6_ecoli_r104"
)

echo "Folder,Type,TP,FP,FN,TN,Mean time per read,Precision,Recall,F-1 Score" > "$OUTPUT_CSV"

extract_fields() {
    local file=$1
    local type=$2
    local tp=$(grep -m 1 "^${type} TP" "$file" | awk '{print $3}')
    local fp=$(grep -m 1 "^${type} FP" "$file" | awk '{print $3}')
    local fn=$(grep -m 1 "^${type} FN" "$file" | awk '{print $3}')
    local tn=$(grep -m 1 "^${type} TN" "$file" | awk '{print $3}')
    local mtpr=$(grep -m 1 "^${type} Mean time per read" "$file" | awk '{print $7}')
    local precision=$(grep -m 1 "^${type} precision" "$file" | awk '{printf "%.5f", $3}')
    local recall=$(grep -m 1 "^${type} recall" "$file" | awk '{printf "%.5f", $3}')
    local f1=$(grep -m 1 "^${type} F-1 score" "$file" | awk '{printf "%.5f", $4}')
    echo "$tp,$fp,$fn,$tn,$mtpr,$precision,$recall,$f1"
}

for folder in "${folders[@]}"; do
    comparison_dir="$folder/comparison"
    if [ -d "$comparison_dir" ]; then
        for file in "$comparison_dir"/*.comparison; do
            if [[ "$file" != *w3* ]]; then
                if [[ "$folder" == "d4_green_algae_r94" && "$file" == *threshold* ]]; then
                    continue
                fi
                for type in "Uncalled" "Sigmap" "RawHash" "RawHash2" "RawHash2_vote"; do
                    if [[ "$folder" == "d4_green_algae_r94" && "$type" == "Uncalled" ]]; then
                        # Skip Uncalled type for d4_green_algae_r94
                        continue
                    fi
                    fields=$(extract_fields "$file" "$type")
                    echo "$folder,$type,$fields" >> "$OUTPUT_CSV"
                done
            fi
        done

        if [[ "$folder" == "d4_green_algae_r94" ]]; then
            for threshold in {9..2}; do
                file="$comparison_dir/d4_green_algae_r94_rawhash2_threshold_${threshold}_sensitive.comparison"
                if [[ -f "$file" ]]; then
                    type="RawHash2_vote"
                    fields=$(extract_fields "$file" "$type")
                    echo "$folder,${type}_threshold_${threshold},$fields" >> "$OUTPUT_CSV"
                fi
            done
        fi
    fi
done

