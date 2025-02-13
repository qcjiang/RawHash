#include "rmap.h"
#include <assert.h>
#include "kthread.h"
#include "rh_kvec.h"
#include "rutils.h"
#include "rsketch.h"
#include "revent.h"
#include "rseed.h"
#include "sequence_until.h"
#include "dtw.h"
#include "chain.h"

#include <math.h>
#include <float.h>  // for FLT_MAX

#ifdef PROFILERH
double ri_filereadtime = 0.0;
double ri_signaltime = 0.0;
double ri_sketchtime = 0.0;
double ri_seedtime = 0.0;
double ri_chaintime = 0.0;
double ri_maptime = 0.0;
double ri_maptime_multithread = 0.0;
#endif

static ri_tbuf_t *ri_tbuf_init(void)
{
	ri_tbuf_t *b;
	b = (ri_tbuf_t*)calloc(1, sizeof(ri_tbuf_t));
	b->km = ri_km_init();
	return b;
}

static void ri_tbuf_destroy(ri_tbuf_t *b)
{
	if (b == 0) return;
	ri_km_destroy(b->km);
	free(b);
}

/**
 * Find seed matches between a chunk of a raw signal and a reference genome
 *
 * @param km     		thread-local memory pool; using NULL falls back to malloc()
 * @return seed_hits    seed hits
 *               		a[i].x = strand | ref_id | ref_pos
 *               		a[i].y = flags  | q_span | q_pos
 */
static mm128_t *collect_seed_hits(void *km,
								//   const ri_mapopt_t *opt,
								  int max_occ,
								  int max_max_occ,
                              	  int dist,
								  const ri_idx_t *ri,
								  const char *qname,
								  ri_reg1_t* reg,
								  const mm128_v *riv,
								  int qlen,
								  int64_t *n_seed_pos,
								  int *rep_len)
								//   int *n_seed_mini,
								//   uint64_t **seed_mini)
{
	int i, n_seed_m;
	ri_seed_t *seed_hits0;
	mm128_t *seed_hits;
	uint32_t mask_pos = (1ULL<<31)-1;
	uint64_t mask_id_shift = (((1ULL<<RI_ID_SHIFT) - 1)<<RI_ID_SHIFT)>>RI_POS_SHIFT;

	seed_hits0 = ri_collect_matches(km, &n_seed_m, qlen, max_occ, max_max_occ, dist, ri, riv, n_seed_pos, rep_len);
	seed_hits = (mm128_t*)ri_kmalloc(km, (*n_seed_pos + reg->n_prev_anchors) * sizeof(mm128_t));
	for (i = 0, *n_seed_pos = 0; i < n_seed_m; ++i) {
		ri_seed_t *s_match = &seed_hits0[i];
		const uint64_t *hits = s_match->cr;
		uint32_t k;
		for (k = 0; k < s_match->n; ++k) {
			uint32_t is_self = 0, ref_pos = (uint32_t)(hits[k]>>RI_POS_SHIFT)&mask_pos;
			mm128_t *p;

			char *ref_name;
			if(ri->flag&RI_I_SIG_TARGET) ref_name = ri->sig[hits[k]>>RI_ID_SHIFT].name;
			else ref_name = ri->seq[hits[k]>>RI_ID_SHIFT].name;

			if(strcmp(qname, ref_name) == 0) continue;
			// if (skip_seed(opt->flag, hits[k], s_match, qname, qlen, ri, &is_self)) continue;
			p = &seed_hits[(*n_seed_pos)++];

			p->x = (hits[k]&mask_id_shift) | ref_pos;
			if(hits[k]&1) p->x |= 1ULL<<63; // reverse strand
			p->y = (uint64_t)s_match->seg_id << RI_SEED_SEG_SHIFT | (uint64_t)s_match->q_span << RI_ID_SHIFT | (uint32_t)((s_match->q_pos>>RI_POS_SHIFT)+reg->offset);
			if (s_match->is_tandem) p->y |= RI_SEED_TANDEM;
			if (is_self) p->y |= RI_SEED_SELF;
		}
	}
	if(seed_hits0){ri_kfree(km, seed_hits0); seed_hits0 = NULL;}

	//memcpy reg->prev_anchors to seed_hits starting from index *n_seed_pos
	if(reg->n_prev_anchors > 0){
		memcpy(seed_hits + *n_seed_pos, reg->prev_anchors, reg->n_prev_anchors * sizeof(mm128_t));
		*n_seed_pos += reg->n_prev_anchors;
		if(reg->prev_anchors) {ri_kfree(km, reg->prev_anchors); reg->prev_anchors = NULL;}
		reg->n_prev_anchors = 0;
	}

	radix_sort_128x(seed_hits, seed_hits + *n_seed_pos);
	return seed_hits;
}

void align_chain(mm_reg1_t *chain, mm128_t* anchors, const ri_idx_t *ri, const float* read_events, const uint32_t n_read_events, const ri_mapopt_t *opt, bool cigar=false, float min_score=-1e10){
	int rid = chain->rid;
	int rs;
	if(ri->flag&RI_I_SIG_TARGET) rs = chain->rev?(uint32_t)(ri->sig[rid].l_sig+1-chain->re):chain->rs;
	else rs = chain->rev?(uint32_t)(ri->seq[rid].len+1-chain->re):chain->rs;
	float* ref = (chain->rev)?ri->R[rid]:ri->F[rid];
	// uint32_t r_len = (chain->rev)?ri->r_l_sig[rid]:ri->f_l_sig[rid];

	float dtw_cost = 0.0f;
	uint32_t num_aligned_read_events = 0;
	if(opt->dtw_border_constraint == RI_M_DTW_BORDER_CONSTRAINT_GLOBAL){
		float* revents = ref + chain->rs;
		const uint32_t rlen = chain->re - chain->rs + 1;

		const float* qevents = read_events + chain->qs;
		const uint32_t qlen = chain->qe - chain->qs + 1;

		float max_attainable_score = qlen*opt->dtw_match_bonus;
		if(max_attainable_score < min_score){
			chain->alignment_score = -1e10;
			return;
		}
		if(opt->dtw_fill_method == RI_M_DTW_FILL_METHOD_FULL){
			dtw_cost = DTW_global(qevents, qlen, revents, rlen);
		}
		else{
			int band_radius = std::max(1, (int)(qlen*opt->dtw_band_radius_frac));
			dtw_cost = DTW_global_slantedbanded_antidiagonalwise(qevents, qlen, revents, rlen, band_radius);
		}
		num_aligned_read_events = qlen;
	}
	else if(opt->dtw_border_constraint == RI_M_DTW_BORDER_CONSTRAINT_SPARSE){
		uint32_t alignment_parts = chain->cnt-1;
		std::vector<alignment_element> alignment;

		//these are only needed for the early termination condition
		const uint32_t qFulllen = chain->qe - chain->qs + 1;
		float current_max_attainable_score = qFulllen*opt->dtw_match_bonus; //max score if the remaining part of the read were matching perfectly

		for(size_t alignment_part=0; alignment_part<alignment_parts; alignment_part++){
			//TODO: some of these could be merged when they are very short, for performance
			const mm128_t &start_anchor = anchors[alignment_part];
			const mm128_t &end_anchor = anchors[alignment_part+1];

			float* revents = ref + (uint32_t)start_anchor.x;
			const uint32_t rlen =  (uint32_t)end_anchor.x - (uint32_t)start_anchor.x + 1;

			const float* qevents = read_events + (uint32_t)start_anchor.y;
			const uint32_t qlen = (uint32_t)end_anchor.y - (uint32_t)start_anchor.y + 1;
			if(current_max_attainable_score < min_score){
				chain->alignment_score = -1e10;
				return;
			}

			bool exclude_last_element = (alignment_part != alignment_parts-1);
			float sub_dtw_cost;
			if(opt->dtw_fill_method == RI_M_DTW_FILL_METHOD_FULL){
				sub_dtw_cost = DTW_global(qevents, qlen, revents, rlen, exclude_last_element);
			}
			else{
				int band_radius = std::max(1, (int)(qlen*opt->dtw_band_radius_frac));
				sub_dtw_cost = DTW_global_slantedbanded_antidiagonalwise(qevents, qlen, revents, rlen, band_radius, exclude_last_element);
			}
			dtw_cost += sub_dtw_cost;
			current_max_attainable_score -= sub_dtw_cost;
			num_aligned_read_events += qlen;
		}
	}
	else{
		fprintf(stderr, "ERROR: invalid border constraint\n");
		exit(EXIT_FAILURE);
	}

	chain->alignment_score = num_aligned_read_events*opt->dtw_match_bonus - dtw_cost;

	if(opt->flag & RI_M_DTW_LOG_SCORES){
		// char str[256];
		fprintf(stderr, "chaining_score=%d alignment_score=%f\n", chain->score, chain->alignment_score);
		// fprintf(stderr, str);
	}
}

void ri_map_frag(const ri_idx_t *ri,
				const uint32_t s_len,
				const float *sig,
				ri_reg1_t* reg,
				ri_tbuf_t *b,
				const ri_mapopt_t *opt,
				const char *qname,
				double* mean_sum,
				double* std_dev_sum,
				uint32_t* n_events_sum,
				const uint32_t c_count = 0)
{	
	uint32_t n_events = 0;

	#ifdef PROFILERH
	double signal_t = ri_realtime();
	#endif
	float* events = detect_events(b->km, s_len, sig, opt->window_length1, opt->window_length2, opt->threshold1, opt->threshold2, opt->peak_height, mean_sum, std_dev_sum, n_events_sum, &n_events);
	#ifdef PROFILERH
	ri_signaltime += ri_realtime() - signal_t;
	#endif

	if(n_events < opt->min_events) {
		if(events){ri_kfree(b->km, events); events = NULL;}
		return;
	}

	//Copy events to the end of reg->events with ri_krealloc
	if(opt->flag&RI_M_DTW_EVALUATE_CHAINS){
		reg->events = (float*)ri_krealloc(b->km, reg->events, (reg->offset+n_events) * sizeof(float));
		memcpy(reg->events + reg->offset, events, n_events * sizeof(float));
	}

	#ifdef PROFILERH
	double sketch_t = ri_realtime();
	#endif

	//Sketching
	mm128_v riv = {0,0,0};
	ri_sketch(b->km, events, 0, 0, n_events, ri->diff, ri->w, ri->e, ri->n, ri->q, ri->k, ri->fine_min, ri->fine_max, ri->fine_range, &riv);
	
	if(events){ri_kfree(b->km, events); events = NULL;}
	// if (opt->q_occ_frac > 0.0f) ri_seed_mz_flt(b->km, &riv, opt->mid_occ, opt->q_occ_frac);

	#ifdef PROFILERH
	ri_sketchtime += ri_realtime() - sketch_t;
	#endif

	#ifdef PROFILERH
	double seed_t = ri_realtime();
	#endif

	// int n_seed_m, n_seed_mini;
	int rep_len;
	int64_t n_seed_pos;
	// ri_seed_t *seed_matches;
	mm128_t *seed_hits;
	uint64_t *u;
	uint32_t hash;
	// uint64_t *seed_mini;

	//Seeding
	seed_hits = collect_seed_hits(b->km, opt->mid_occ, opt->max_max_occ, opt->occ_dist, ri, qname, reg, &riv, n_events, &n_seed_pos, &rep_len);
	if(riv.a){ri_kfree(b->km, riv.a); riv.a = NULL; riv.n = riv.m = 0;}
	// ri_kfree(b->km, seed_mini);

	#ifdef PROFILERH
	ri_seedtime += ri_realtime() - seed_t;
	#endif

	#ifdef PROFILERH
	double chain_t = ri_realtime();
	#endif

	//Chaining (DP or RMQ)
	int max_gap = (opt->max_target_gap_length > opt->max_query_gap_length)?opt->max_target_gap_length:opt->max_query_gap_length;
	float chn_pen_gap = opt->chain_gap_scale * 0.01 * (ri->e + ri->k - 1), chn_pen_skip = opt->chain_skip_scale * 0.01 * (ri->e + ri->k - 1);
	if(!(opt->flag & RI_M_RMQ))
		seed_hits = mg_lchain_dp(opt->max_target_gap_length, opt->max_query_gap_length, opt->bw, opt->max_num_skips, 
								opt->max_chain_iter, opt->min_num_anchors,opt->min_chaining_score,chn_pen_gap,
								chn_pen_skip,&n_seed_pos,seed_hits, &(reg->prev_anchors), &(reg->n_cregs), &u, b->km);
	else
		seed_hits = mg_lchain_rmq(max_gap, opt->rmq_inner_dist, opt->bw, opt->max_num_skips, opt->rmq_size_cap,
							 	  opt->min_num_anchors,opt->min_chaining_score,chn_pen_gap,chn_pen_skip,&n_seed_pos,
								  seed_hits, &(reg->prev_anchors), &(reg->n_cregs), &u, b->km);

	if(opt->bw_long > opt->bw){
		seed_hits = mg_lchain_rmq(max_gap, opt->rmq_inner_dist, opt->bw_long, opt->max_num_skips, opt->rmq_size_cap,
							 	  opt->min_num_anchors,opt->min_chaining_score,chn_pen_gap,chn_pen_skip,&n_seed_pos,
								  seed_hits, &(reg->prev_anchors), &(reg->n_cregs), &u, b->km);
	}

	reg->n_prev_anchors = 0;
	if(n_seed_pos>0)reg->n_prev_anchors = n_seed_pos;
	else if(reg->prev_anchors){ri_kfree(b->km, reg->prev_anchors); reg->prev_anchors = NULL;}

	hash = 0;
	hash ^= __ac_Wang_hash(reg->offset+n_events) + __ac_Wang_hash(11);
	hash  = __ac_Wang_hash(hash);

	//Find primary chains
	reg->creg = mm_gen_regs(b->km, hash, reg->offset+n_events, reg->n_cregs, u, seed_hits);
	mm_set_parent(b->km, opt->mask_level, opt->mask_len, reg->n_cregs, reg->creg, opt->flag&RI_M_HARD_MLEVEL, opt->alt_drop);
	mm_select_sub(b->km, opt->pri_ratio, opt->best_n, 1, opt->max_target_gap_length * 0.8, &(reg->n_cregs), reg->creg);

	if(opt->flag&RI_M_DTW_EVALUATE_CHAINS){
		//this could be slightly more agressive and be set to opt->dtw_min_score immediately,
		//but starting with 0 if clearer
		float best_found_alignment = 0.0f;
		int32_t i, k;
		
		for(i = 0; i < reg->n_cregs; ++i){
			k = reg->creg[i].as;
			align_chain(&reg->creg[i], seed_hits + k, ri, reg->events, reg->offset+n_events, opt, false, best_found_alignment);

			if(reg->creg[i].alignment_score >= opt->dtw_min_score){
				if(reg->creg[i].alignment_score > best_found_alignment){
					best_found_alignment = reg->creg[i].alignment_score;
				}
			}
			else if(reg->creg[i].alignment_score < opt->dtw_min_score && reg->creg[i].alignment_score < 0)
				reg->creg[i].alignment_score = (opt->dtw_min_score > 0)?0:opt->dtw_min_score;
		}
	}

	//Set MAPQ TODO: integrate alignment score within mapq
	mm_set_mapq(b->km, reg->n_cregs, reg->creg, opt->min_chaining_score, rep_len, (opt->flag&RI_M_DTW_EVALUATE_CHAINS)?1:0);

	if(seed_hits){ri_kfree(b->km, seed_hits); seed_hits = NULL;}
	if(u){ri_kfree(b->km, u); u = NULL;}

	#ifdef PROFILERH
	ri_chaintime += ri_realtime() - chain_t;
	#endif

	reg->offset += n_events;
}

static void map_worker_for(void *_data,
						   long i,
						   int tid) // kt_for() callback
{
    step_mt *s = (step_mt*)_data; //s->sig and s->n_sig (signals read in this step and num of them)
	const ri_mapopt_t *opt = s->p->opt;
	ri_tbuf_t* b = s->buf[tid];
	ri_reg1_t* reg0 = s->reg[i];
	reg0->prev_anchors = NULL, reg0->creg = NULL, reg0->events = NULL;
	reg0->offset = 0, reg0->n_prev_anchors = 0, reg0->n_cregs = 0;

	ri_sig_t* sig = s->sig[i];

	uint32_t qlen = sig->l_sig;
	uint32_t l_chunk = (opt->chunk_size > qlen)?qlen:opt->chunk_size;
	uint32_t max_chunk =  (opt->flag&RI_M_NO_ADAPTIVE)?(qlen/(l_chunk+1))+1:opt->max_num_chunk;
	uint32_t s_qs, s_qe = l_chunk;

	uint32_t c_count = 0;
	reg0->n_maps = 0;

	double t = ri_realtime();

	double mean_sum = 0, std_dev_sum = 0;
	uint32_t n_events_sum = 0;

	for (s_qs = c_count = 0; s_qs < qlen && c_count < max_chunk; s_qs += l_chunk, ++c_count) {
		s_qe = s_qs + l_chunk;
		if(s_qe > qlen) s_qe = qlen;

		if(reg0->creg){free(reg0->creg); reg0->creg = NULL; reg0->n_cregs = 0;}

		ri_map_frag(s->p->ri, (const uint32_t)s_qe-s_qs, (const float*)&(sig->sig[s_qs]), reg0, b, opt, sig->name, &mean_sum, &std_dev_sum, &n_events_sum);

		int n_chains = (opt->flag&RI_M_ALL_CHAINS || reg0->n_cregs < 1)?reg0->n_cregs:1;

		if (reg0->n_cregs == 1 && ((reg0->creg[0].mapq >= opt->min_mapq) || (opt->flag&RI_M_DTW_EVALUATE_CHAINS && reg0->creg[0].alignment_score >= opt->dtw_min_score))) {
			reg0->n_maps++;
			reg0->maps = (ri_map_t*)ri_krealloc(0, reg0->maps, reg0->n_maps*sizeof(ri_map_t));
			reg0->maps[reg0->n_maps-1].c_id = 0;
			break;
		}

		//TODO make n_cregs a parameter of best n mappings
		float meanC = 0, meanQ = 0;
		// if(opt->flag&RI_M_DTW_EVALUATE_CHAINS){
		// 	uint32_t chain_cnt = 0;
		// 	for (uint32_t c_ind = 0; c_ind < reg0->n_cregs; ++c_ind){
		// 		if(reg0->creg[c_ind].alignment_score < opt->dtw_min_score) continue;
		// 		chain_cnt++;
		// 		meanC += reg0->creg[c_ind].score;
		// 		meanQ += reg0->creg[c_ind].mapq;
		// 		// meanA += reg0->creg[c_ind].alignment_score;
		// 	}
		// 	if(chain_cnt){meanC /= chain_cnt; meanQ /= chain_cnt;}
		// }
		// else{
		for (int32_t c_ind = 0; c_ind < reg0->n_cregs; ++c_ind){
			meanC += reg0->creg[c_ind].score;
			meanQ += reg0->creg[c_ind].mapq;
		}
		if(reg0->n_cregs > 0){meanC /= reg0->n_cregs; meanQ /= reg0->n_cregs;}
		// }
		
		for(int ic = 0; ic < n_chains; ++ic){
			float r_bestma = 0.0f, r_bestmq = 0.0f, r_bestmc = 0.0f, r_bestq = 0.0f;
			float bestQ = 0.0f, bestC = 0.0f, bestA = 0.0f, weighted_sum = 0.0f;
			bestQ = reg0->creg[ic].mapq;
			bestC = reg0->creg[ic].score;

			if(opt->flag&RI_M_DTW_EVALUATE_CHAINS){
				bestA = reg0->creg[ic].alignment_score;
				if(n_chains == 1){ //no all-vs-all overlap mod
					uint32_t best_ind = 0;
					for(int i = 1; i < reg0->n_cregs; ++i){
						if(reg0->creg[i].alignment_score > bestA){
							bestA = reg0->creg[i].alignment_score;
							best_ind = i;
						}
					}
					ic = best_ind;
					bestQ = reg0->creg[ic].mapq;
					bestC = reg0->creg[ic].score;
				}
				if(bestA >= opt->dtw_min_score){
					// r_bestma = (bestA > 0)?(1.0f - (meanA/bestA)):0.0f; if(r_bestma < 0) r_bestma = 0.0f;
					r_bestma = (bestA > 0)?(bestA/50.0f):0.0f; if(r_bestma < 0) r_bestma = 0.0f;
					r_bestmq = (bestQ > 0)?(1.0f - (meanQ/bestQ)):0.0f; if(r_bestmq < 0) r_bestmq = 0.0f;
					r_bestmc = (bestC > 0)?(1.0f - (meanC/bestC)):0.0f; if(r_bestmc < 0) r_bestmc = 0.0f;

					weighted_sum = opt->w_bestma*r_bestma + opt->w_bestmq*r_bestmq + opt->w_bestmc*r_bestmc;
				}
			}else{
				r_bestq = (bestQ > 0)?(bestQ/30.0f):0.0f; if(r_bestq > 1) r_bestq = 1.0f;
				r_bestmq = (bestQ > 0)?(1.0f - (meanQ/bestQ)):0.0f; if(r_bestmq < 0) r_bestmq = 0.0f;
				r_bestmc = (bestC > 0)?(1.0f - (meanC/bestC)):0.0f; if(r_bestmc < 0) r_bestmc = 0.0f;

				weighted_sum = opt->w_bestq*r_bestq + opt->w_bestmq*r_bestmq + opt->w_bestmc*r_bestmc;
			}
			
			// Compare the weighted sum against a threshold to make the decision
			if (weighted_sum >= opt->w_threshold) {
				reg0->n_maps++;
				reg0->maps = (ri_map_t*)ri_krealloc(0, reg0->maps, reg0->n_maps*sizeof(ri_map_t));
				reg0->maps[reg0->n_maps-1].c_id = ic;
				// fprintf(stderr, "Aligned ic: %d\n", ic);
			}
		}

		if(reg0->n_maps > 0) break;
	} double mapping_time = ri_realtime() - t;

	#ifdef PROFILERH
	ri_maptime += mapping_time;
	#endif

	if (c_count > 0 && (s_qs >= qlen || c_count == max_chunk)) --c_count;

	float read_position_scale = (reg0->offset == 0)?0.0f:(opt->sample_per_base == 0)?0.0f:((float)(c_count+1)*l_chunk/reg0->offset)/opt->sample_per_base;
	mm_reg1_t* chains = reg0->creg;

	if(!chains) {reg0->n_cregs = 0;}
	float mean_chain_score = 0;

	if(reg0->n_maps == 0 && reg0->creg && reg0->creg[0].mapq > opt->min_mapq){
		reg0->n_maps++;
		reg0->maps = (ri_map_t*)ri_krealloc(0, reg0->maps, reg0->n_maps*sizeof(ri_map_t));
		reg0->maps[reg0->n_maps-1].c_id = 0;
	}

	if (reg0->n_maps == 0){
		reg0->maps = (ri_map_t*)ri_kcalloc(0, 1, sizeof(ri_map_t));
		char *tags = (char *)malloc(1024 * sizeof(char));
		tags[0] = '\0'; // make it an empty string
		char buffer[256]; // temporary buffer

		sprintf(buffer, "mt:f:%.6f", mapping_time * 1000); strcat(tags, buffer);
		sprintf(buffer, "\tci:i:%d", c_count + 1); strcat(tags, buffer);
		sprintf(buffer, "\tsl:i:%d", qlen); strcat(tags, buffer);
		if (reg0->n_cregs >= 1) {
			sprintf(buffer, "\tcm:i:%d", chains[0].cnt); strcat(tags, buffer);
			sprintf(buffer, "\tnc:i:%d", reg0->n_cregs); strcat(tags, buffer);
			sprintf(buffer, "\ts1:i:%d", chains[0].score); strcat(tags, buffer);
			// sprintf(buffer, "\ts2:i:%d", reg0->n_cregs > 1 ? chains[1].score : 0); strcat(tags, buffer);
			sprintf(buffer, "\tsm:f:%.2f", mean_chain_score); strcat(tags, buffer);
		}else {
			sprintf(buffer, "\tcm:i:0"); strcat(tags, buffer);
			sprintf(buffer, "\tnc:i:0"); strcat(tags, buffer);
			sprintf(buffer, "\ts1:i:0"); strcat(tags, buffer);
			// sprintf(buffer, "\ts2:i:0"); strcat(tags, buffer);
			sprintf(buffer, "\tsm:f:0"); strcat(tags, buffer);
		}

		reg0->read_id = sig->rid;
		reg0->read_name = sig->name;
		reg0->maps[0].read_length = (s->p->ri->flag&RI_I_SIG_TARGET)?reg0->offset:(uint32_t)(read_position_scale * reg0->offset);
		reg0->maps[0].c_id = 0;
		reg0->maps[0].ref_id = 0;
		reg0->maps[0].read_start_position = 0;
		reg0->maps[0].read_end_position = 0;
		reg0->maps[0].fragment_start_position = 0;
		reg0->maps[0].fragment_length = 0;
		reg0->maps[0].mapq = 0;
		reg0->maps[0].rev = 0;
		reg0->maps[0].mapped = 0;
		reg0->maps[0].tags = tags;
	}else{
		for(uint32_t m = 0; m < reg0->n_maps; ++m){
			char *tags = (char *)malloc(1024 * sizeof(char));
			uint32_t c_id = reg0->maps[m].c_id;
			tags[0] = '\0'; // make it an empty string
			char buffer[256]; // temporary buffer
			sprintf(buffer, "mt:f:%.6f", mapping_time * 1000); strcat(tags, buffer);
			sprintf(buffer, "\tci:i:%d", c_count + 1); strcat(tags, buffer);
			sprintf(buffer, "\tsl:i:%d", qlen); strcat(tags, buffer);
			sprintf(buffer, "\tcm:i:%d", chains[c_id].cnt); strcat(tags, buffer);
			sprintf(buffer, "\tnc:i:%d", reg0->n_cregs); strcat(tags, buffer);
			sprintf(buffer, "\ts1:i:%d", chains[c_id].score); strcat(tags, buffer);
			// sprintf(buffer, "\ts2:i:%d", reg0->n_cregs > 1 ? chains[1].score : 0); strcat(tags, buffer);
			sprintf(buffer, "\tsm:f:%.2f", mean_chain_score); strcat(tags, buffer);

			reg0->read_id = sig->rid;
			reg0->read_name = sig->name;
			reg0->maps[m].read_length = (s->p->ri->flag&RI_I_SIG_TARGET)?(reg0->offset):(uint32_t)(read_position_scale*chains[c_id].qe);
			reg0->maps[m].ref_id = chains[c_id].rid;
			reg0->maps[m].read_start_position = (s->p->ri->flag&RI_I_SIG_TARGET)?chains[c_id].qs:(uint32_t)(read_position_scale*chains[c_id].qs);
			reg0->maps[m].read_end_position = (s->p->ri->flag&RI_I_SIG_TARGET)?chains[c_id].qe:(uint32_t)(read_position_scale*chains[c_id].qe);
			if(s->p->ri->flag&RI_I_SIG_TARGET) reg0->maps[m].fragment_start_position = chains[c_id].rev?(uint32_t)(s->p->ri->sig[chains[c_id].rid].l_sig+1-chains[c_id].re):chains[c_id].rs;
			else reg0->maps[m].fragment_start_position = chains[c_id].rev?(uint32_t)(s->p->ri->seq[chains[c_id].rid].len+1-chains[c_id].re):chains[c_id].rs;
			reg0->maps[m].fragment_length = (uint32_t)(chains[c_id].re - chains[c_id].rs + 1);
			reg0->maps[m].mapq = chains[c_id].mapq;
			reg0->maps[m].rev = (chains[c_id].rev == 1)?1:0;
			reg0->maps[m].mapped = 1; 
			reg0->maps[m].tags = tags;
		}
	}

	if(reg0->prev_anchors) {ri_kfree(b->km, reg0->prev_anchors); reg0->prev_anchors = NULL; reg0->n_prev_anchors = 0;}
	if(reg0->creg){free(reg0->creg); reg0->creg = NULL; reg0->n_cregs = 0;}
	if(reg0->events){ri_kfree(b->km, reg0->events); reg0->events = NULL; reg0->offset = 0;}

	if (b->km) {
		ri_km_stat_t kmst;
		ri_km_stat(b->km, &kmst);
		// assert(kmst.n_blocks == kmst.n_cores);
		ri_km_destroy(b->km);
		b->km = ri_km_init();
	}
}

ri_sig_t** ri_sig_read_frag(pipeline_mt *pl,
							int64_t chunk_size,
							int *n_)
{	
	*n_ = 0;
	if (pl->n_fp < 1) return 0;

	//Debugging for sweeping purposes
	// if(pl->su_nreads >= 1000) return 0;

	int64_t size = 0;
	rhsig_v rsigv = {0,0,0};
	rh_kv_resize(ri_sig_t*, 0, rsigv, 4000);

	while (pl->fp) {
		//Reading data in bulk if the buffer is emptied
		while(pl->fp && pl->fp->cur_read == pl->fp->num_read){
			ri_sig_close(pl->fp);
			if(pl->cur_f < pl->n_f){
				if((pl->fp = open_sig(pl->f[pl->cur_f++])) == 0) break;
			}else if(pl->cur_fp < pl->n_fp){
				if(pl->f){
					for(int i = 0; i < pl->n_f; ++i) 
						if(pl->f[i]){free(pl->f[i]); pl->f[i] = NULL;}
					free(pl->f); pl->f = NULL;
				}
				pl->n_f = 0; pl->cur_f = 0;

				ri_char_v fnames = {0,0,0};
				rh_kv_resize(char*, 0, fnames, 256);
				find_sfiles(pl->fn[pl->cur_fp++], &fnames);
				pl->f =  fnames.a;
				if(!fnames.n || ((pl->fp = open_sig(pl->f[pl->cur_f++])) == 0)) break;
				pl->n_f = fnames.n;
				// ++n_read;
			}else {pl->fp = 0; break;}
		}

		if(!pl->fp || pl->fp->cur_read == pl->fp->num_read) break;
		
		ri_sig_t *s = (ri_sig_t*)calloc(1, sizeof(ri_sig_t));
		rh_kv_push(ri_sig_t*, 0, rsigv, s);
		ri_read_sig(pl->fp, s);
		size += s->l_sig;

		if(size >= chunk_size) break;
		
		//Debugging for sweeping purposes
		// pl->su_nreads++;
		// if(pl->su_nreads >= 1000) break;
	}

	ri_sig_t** a = 0;
	if(rsigv.n) a = rsigv.a;
	else rh_kv_destroy(rsigv);
	*n_ = rsigv.n;

	return a;
}

static void *map_worker_pipeline(void *shared,
								int step,
								void *in)
{
	int i, k;
    pipeline_mt *p = (pipeline_mt*)shared;
    if (step == 0) { // step 0: read sequences
		#ifdef PROFILERH
		double file_t = ri_realtime();
		#endif
        step_mt *s;
        s = (step_mt*)calloc(1, sizeof(step_mt));

		s->sig = ri_sig_read_frag(p, p->mini_batch_size, &s->n_sig);
		if (s->n_sig && !p->su_stop) {
			s->p = p;
			for (i = 0; i < s->n_sig; ++i)
				(*(s->sig[i])).rid = p->n_processed++;
			s->buf = (ri_tbuf_t**)calloc(p->n_threads, sizeof(ri_tbuf_t*));
			for (i = 0; i < p->n_threads; ++i)
				s->buf[i] = ri_tbuf_init();
			s->reg = (ri_reg1_t**)calloc(s->n_sig, sizeof(ri_reg1_t*));
			for(i = 0; i < s->n_sig; ++i)
				s->reg[i] = (ri_reg1_t*)calloc(1, sizeof(ri_reg1_t));
			return s;
		} else if(s){
			if(s->sig) {free(s->sig); s->sig = NULL;}
			free(s); s = NULL;
		}
		#ifdef PROFILERH
		ri_filereadtime += ri_realtime() - file_t;
		#endif
    } else if (step == 1) { // step 1: detect events
		step_mt *s = (step_mt*)in;
		#ifdef PROFILERH
		double map_multit = ri_realtime();
		#endif
		if(!p->su_stop) kt_for(p->n_threads, map_worker_for, in, s->n_sig);
		#ifdef PROFILERH
		ri_maptime_multithread += ri_realtime() - map_multit;
		#endif

		//Debugging for sweeping purposes
		// if(p->su_nreads >= 1000) p->su_stop = p->su_nreads;

		if(p->opt->flag & RI_M_SEQUENCEUNTIL && !p->su_stop){
			const ri_idx_t *ri = p->ri;
			for (k = 0; k < s->n_sig; ++k) {
				if(s->reg[k] && s->reg[k]->maps[0].ref_id < ri->n_seq && s->reg[k]->read_name && s->reg[k]->maps[0].mapped){
					ri_reg1_t* reg0 = s->reg[k];
					p->su_c_estimations[reg0->maps[0].ref_id] += reg0->maps[0].fragment_length;
					p->ab_count += reg0->maps[0].fragment_length;
					p->su_nreads++;
					if(p->su_nreads > p->opt->tmin_reads && !(p->su_nreads%p->opt->ttest_freq)){
						//calculate abundance
						for(uint32_t ce = 0; ce < ri->n_seq; ++ce){
							p->su_estimations[p->su_cur][ce] =  (float)p->su_c_estimations[ce]/p->ab_count;
						}
						
						if(++p->su_cur >= p->opt->tn_samples) p->su_cur = 0;
						if(p->su_nestimations++ >= p->opt->tn_samples){
							if(find_outlier((const float**)p->su_estimations, ri->n_seq, p->opt->tn_samples) <= p->opt->t_threshold){
								//sending the stop signal.
								p->su_stop = k+1;
								fprintf(stderr, "[M::%s] Sequence Until is activated, stopping sequencing after processing %d mapped reads\n", __func__, p->su_nreads);
								break;
							}
						}
					}
				}
			}
		}
		return in;
    } else if (step == 2) { // step 2: output
		// void *km = 0;
        step_mt *s = (step_mt*)in;
		const ri_idx_t *ri = p->ri;
		for (k = 0; k < s->n_sig; ++k) {
			if(s->reg[k]){
				ri_reg1_t* reg0 = s->reg[k];
				// char strand = reg0->rev?'-':'+';

				// fprintf(stderr, "%s %d %d\n", reg0->read_name, reg0->n_maps, reg0->maps[0].mapped);
				
				if(reg0->read_name){
					if(reg0->n_maps > 0 && (!p->su_stop || k < p->su_stop)){
						for(uint32_t m = 0; m < reg0->n_maps; ++m){
							if(reg0->maps[m].ref_id < ri->n_seq)
								fprintf(stdout, "%s\t%u\t%u\t%u\t%c\t%s\t%u\t%u\t%u\t%u\t%u\t%u\t%s\n", 
												reg0->read_name,
												reg0->maps[m].read_length,
												reg0->maps[m].read_start_position,
												reg0->maps[m].read_end_position, 
												reg0->maps[m].rev?'-':'+',
												(ri->flag&RI_I_SIG_TARGET)?ri->sig[reg0->maps[m].ref_id].name:ri->seq[reg0->maps[m].ref_id].name,
												(ri->flag&RI_I_SIG_TARGET)?ri->sig[reg0->maps[m].ref_id].l_sig:ri->seq[reg0->maps[m].ref_id].len,
												reg0->maps[m].fragment_start_position,
												reg0->maps[m].fragment_start_position + reg0->maps[m].fragment_length, 
												reg0->maps[m].read_end_position-reg0->maps[m].read_start_position-1, 
												reg0->maps[m].fragment_length,
												reg0->maps[m].mapq,
												reg0->maps[m].tags);
							if(reg0->maps[m].tags) {free(reg0->maps[m].tags); reg0->maps[m].tags = NULL;}
						}
					}else{
						fprintf(stdout, "%s\t%u\t*\t*\t*\t*\t*\t*\t*\t*\t*\t%u\t%s\n", 
						reg0->read_name, 
						reg0->maps[0].read_length, 
						reg0->maps[0].mapq, 
						reg0->maps[0].tags);

						if(reg0->maps[0].tags) {free(reg0->maps[0].tags); reg0->maps[0].tags = NULL;}
					}
				}

				// if(reg0->tags) {free(reg0->tags); reg0->tags = NULL;}
				if(reg0->maps){free(reg0->maps); reg0->maps = NULL;}
				free(reg0); s->reg[k] = NULL;
				fflush(stdout);
			}
		}

		for (i = 0; i < p->n_threads; ++i) ri_tbuf_destroy(s->buf[i]);
		if(s->buf){free(s->buf); s->buf = NULL;}
		if(s->reg){free(s->reg); s->reg = NULL;}

		for(int i = 0; i < s->n_sig; ++i){
			ri_sig_t *curS = s->sig[i];
			if(!curS) continue;
			if(curS->sig){free(curS->sig); curS->sig = NULL;}
			if(curS->name){free(curS->name); curS->name = NULL;}
			free(curS); s->sig[i] = NULL;
		}
		if(s->sig){free(s->sig); s->sig = NULL;}
		free(s); s = NULL;
	}
    return 0;
}

int ri_map_file(const ri_idx_t *idx,
				const char *fn,
				const ri_mapopt_t *opt,
				int n_threads)
{
	return ri_map_file_frag(idx, 1, &fn, opt, n_threads);
}

int ri_map_file_frag(const ri_idx_t *idx,
					int n_segs,
					const char **fn,
					const ri_mapopt_t *opt,
					int n_threads)
{
	int pl_threads;
	pipeline_mt pl;
	if (n_segs < 1) return -1;
	memset(&pl, 0, sizeof(pipeline_mt));
	pl.n_fp = n_segs;
	pl.n_f = 0; pl.cur_f = 0;
	ri_char_v fnames = {0,0,0};
	rh_kv_resize(char*, 0, fnames, 256);
	find_sfiles(fn[0], &fnames);
	pl.f =  fnames.a;
	if(!fnames.n || ((pl.fp = open_sig(pl.f[0])) == 0)){rh_kv_destroy(fnames); return -1;}
	if (pl.fp == 0){rh_kv_destroy(fnames); return -1;}
	pl.fn = fn;
	pl.n_f = fnames.n;
	pl.cur_fp = 1;
	pl.cur_f = 1;
	pl.opt = opt, pl.ri = idx;
	pl.n_threads = n_threads > 1? n_threads : 1;
	pl.mini_batch_size = opt->mini_batch_size;
	pl_threads = pl.n_threads == 1?1:2;
	pl.su_stop = 0;

	if(opt->flag & RI_M_SEQUENCEUNTIL){
		pl.su_nreads = 0;
		pl.su_nestimations = 0;
		pl.ab_count = 0;
		pl.su_cur = 0;
		pl.su_estimations = (float**)calloc(opt->tn_samples, sizeof(float*));
		for(uint32_t i = 0; i < opt->tn_samples; ++i) pl.su_estimations[i] = (float*)calloc(idx->n_seq, sizeof(float));

		pl.su_c_estimations = (uint32_t*)calloc(idx->n_seq, sizeof(uint32_t));
	}
	
	kt_pipeline(pl_threads, map_worker_pipeline, &pl, 3);

	if(opt->flag & RI_M_SEQUENCEUNTIL){
		// pl.su_nreads = 0;
		// pl.su_nestimations = 0;
		// pl.su_stop = 0;
		if(pl.su_estimations){
			for(uint32_t i = 0; i < opt->tn_samples; ++i) 
				if(pl.su_estimations[i]){free(pl.su_estimations[i]); pl.su_estimations[i] = NULL;}
			free(pl.su_estimations); pl.su_estimations = NULL;
		}

		if(pl.su_c_estimations){free(pl.su_c_estimations); pl.su_c_estimations = NULL;}
	}

	#ifdef PROFILERH
	fprintf(stderr, "\n[M::%s] File read: %.6f sec; Signal-to-event: %.6f sec; Sketching: %.6f sec; Seeding: %.6f sec; Chaining: %.6f sec; Mapping: %.6f sec; Mapping (multi-threaded): %.6f sec\n", __func__, ri_filereadtime, ri_signaltime, ri_sketchtime, ri_seedtime, ri_chaintime, ri_maptime, ri_maptime_multithread);
	#endif

	rh_kv_destroy(fnames);

	return 0;
}