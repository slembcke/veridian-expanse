#include <limits.h>
#include <string.h>

#include "drift_base.h"

#include "tracy/TracyC.h"

static DriftAABB2 rnode_bound(DriftRTree* tree, u16 node_idx){
	DriftAABB2* bounds = tree->n_bound[node_idx].arr;
	uint count = tree->n_count[node_idx];
	
	DriftAABB2 bb = bounds[0];
	for(uint i = 1; i < count; i++) bb = DriftAABB2Merge(bb, bounds[i]);
	return bb;
}

static u16 rnode_find(DriftRTree* tree, u16 node_idx, u16 child_idx){
	u16* child = tree->n_child[node_idx].arr;
	for(uint i = 0; i < RTREE_BRANCH_FACTOR; i++){
		if(child[i] == child_idx) return i;
	}
	DRIFT_ASSERT_HARD(false, "This should be unreachable");
}

static bool rnode_insert(DriftRTree* tree, u16 node_idx, u16 child, DriftAABB2 bb){
	uint count = tree->n_count[node_idx];
	if(count < RTREE_BRANCH_FACTOR){
		tree->n_bound[node_idx].arr[count] = bb;
		tree->n_child[node_idx].arr[count] = child;
		tree->n_count[node_idx] = count + 1;
		return true;
	} else {
		return false;
	}
}

typedef struct {
	DriftRTree* tree;
	u16 idx0, idx1;
	
	uint count;
	DriftAABB2 bound[2*RTREE_BRANCH_FACTOR];
	u16 child[2*RTREE_BRANCH_FACTOR];
	
	DriftAABB2* bb_ptr[2];
} PartitionContext;

// Manhattan distance to nearest center.
static float k_metric(DriftAABB2 bb, DriftVec2 p){return fabsf((bb.l + bb.r) - 2*p.x) + fabsf((bb.b + bb.t) - 2*p.y);}

static void kmeans2(PartitionContext* ctx){
	// Find the min/max bounding box centers.
	DriftVec2 center0 = DriftAABB2Center(ctx->bound[0]);
	DriftAABB2 minmax = {center0.x, center0.y, center0.x, center0.y};
	DriftVec2 center_l = center0, center_b = center0, center_r = center0, center_t = center0;
	for(uint i = 1; i < ctx->count; i++){
		DriftVec2 center = DriftAABB2Center(ctx->bound[i]);
		if(center.x < minmax.l) minmax.l = center.x, center_l = center;
		if(center.x > minmax.r) minmax.r = center.x, center_r = center;
		if(center.y < minmax.b) minmax.b = center.y, center_b = center;
		if(center.y > minmax.t) minmax.t = center.y, center_t = center;
	}
	
	// Use the longest axis as a heuristic to prime the k-means.
	DriftVec2 mean0, mean1;
	if(minmax.r - minmax.l > minmax.t - minmax.b){
		mean0 = center_l, mean1 = center_r;
		DRIFT_ASSERT(mean0.x != mean1.x, "Degenerate split.");
	} else{
		mean0 = center_b, mean1 = center_t;
		DRIFT_ASSERT(mean0.y != mean1.y, "Degenerate split.");
	}
	
	DriftVec2 sum0, sum1;
	float weight0, weight1;
	bool part[2*RTREE_BRANCH_FACTOR];
	for(uint loop = 0; loop < 8; loop++){
		DRIFT_ASSERT(mean0.x != mean1.x && mean0.y != mean1.y, "Matching means.");
		sum0 = sum1 = (DriftVec2){};
		weight0 = weight1 = 0;
		
		for(uint i = 0; i < ctx->count; i++){
			DriftAABB2 bb = ctx->bound[i];
			// Weight by area.
			float weight = (bb.r - bb.l)*(bb.t - bb.b);
			DRIFT_ASSERT(fabsf(weight) < INFINITY, "Infinite weight.");
			if((part[i] = (k_metric(bb, mean0) < k_metric(bb, mean1)))){
				sum0 = DriftVec2FMA(sum0, DriftAABB2Center(bb), weight), weight0 += weight;
			} else {
				sum1 = DriftVec2FMA(sum1, DriftAABB2Center(bb), weight), weight1 += weight;
			}
		}
		
#if DRIFT_DEBUG
		uint check = 0;
		for(uint i= 0; i <ctx->count; i++) check += part[i];
		DRIFT_ASSERT(check != 0 && check <= RTREE_BRANCH_FACTOR, "Bad split");
#endif
		
		mean0 = DriftVec2Mul(sum0, 1/weight0);
		mean1 = DriftVec2Mul(sum1, 1/weight1);
		DRIFT_ASSERT(finitef(mean0.x) && finitef(mean0.y) && finitef(mean1.x) && finitef(mean1.y), "Non-finite means.");
	}
	
	// Insert into output nodes.
	ctx->tree->n_count[ctx->idx0] = 0;
	ctx->tree->n_count[ctx->idx1] = 0;
	
	for(uint i = 0; i < ctx->count; i++){
		DRIFT_ASSERT_HARD(rnode_insert(ctx->tree, part[i] ? ctx->idx0 : ctx->idx1, ctx->child[i], ctx->bound[i]), "Failed to split node.");
	}
	
	*ctx->bb_ptr[0] = rnode_bound(ctx->tree, ctx->idx0);
	*ctx->bb_ptr[1] = rnode_bound(ctx->tree, ctx->idx1);
}

static void partition_insert(PartitionContext* ctx, u16 node_idx){
	DriftAABB2* bound = ctx->tree->n_bound[node_idx].arr;
	u16* child = ctx->tree->n_child[node_idx].arr;
	uint count = ctx->tree->n_count[node_idx];
	
	for(uint i = 0; i < count; i++){
		ctx->bound[ctx->count] = bound[i];
		ctx->child[ctx->count] = child[i];
		ctx->count++;
	}
}

static void partition1(PartitionContext* ctx, u16 node_idx, u16 child_idx, DriftAABB2 bb){
	DRIFT_ASSERT(ctx->idx0 != ctx->idx1, "Non-unique nodes for partitioning");
	ctx->count = 1;
	ctx->bound[0] = bb;
	ctx->child[0] = child_idx;
	partition_insert(ctx, node_idx);
	kmeans2(ctx);
}

static u16 rtree_new_node(DriftRTree* tree){
	// Pop a node from the pool or add a new one.
	return (tree->pool_idx ? tree->pool_arr[--tree->pool_idx] : DriftTablePushRow(&tree->table));
}

static void rtree_insert(DriftRTree* tree, u16 user_idx, DriftAABB2 bb){
	uint stack_count = 1;
	u16 stack_arr[8] = {tree->root};
	
	// Find node to insert into.
	for(u16 node_idx = tree->root; !tree->n_leaf[node_idx];){
		float near_dist = INFINITY;
		u16 near_idx = 0;
		
		DriftAABB2* bound = tree->n_bound[node_idx].arr;
		uint count = tree->n_count[node_idx];
		
		for(uint i = 0; i < count; i++){
			DriftAABB2 bb2 = bound[i];
			// Closest based on manhattan distance.
			float dist = fabsf((bb.l + bb.r) - (bb2.l + bb2.r)) + fabsf((bb.b + bb.t) - (bb2.b + bb2.t));
			if(dist < near_dist) near_dist = dist, near_idx = i;
		}
		
		// Expand to include the new bb.
		bound[near_idx] = DriftAABB2Merge(bound[near_idx], bb);
		
		DRIFT_ASSERT_HARD(stack_count < sizeof(stack_arr)/sizeof(*stack_arr), "DriftRTree stack overflow");
		node_idx = tree->n_child[node_idx].arr[near_idx];
		stack_arr[stack_count++] = node_idx;
	}
	
	// Walk back up the tree splitting nodes as needed.
	u16 child_idx = user_idx;
	while(true){
		u16 node_idx = stack_arr[--stack_count];
		if(rnode_insert(tree, node_idx, child_idx, bb)) break;
		
		// Insertion failed. Need to partition the results.
		u16 part_idx = rtree_new_node(tree);
		tree->n_leaf[part_idx] = tree->n_leaf[node_idx];
		
		if(stack_count > 0){
			u16 parent_idx = stack_arr[stack_count - 1];
			
			partition1(&(PartitionContext){
				.tree = tree, .idx0 = node_idx, .idx1 = part_idx,
				.bb_ptr[0] = tree->n_bound[parent_idx].arr + rnode_find(tree, parent_idx, node_idx),
				.bb_ptr[1] = &bb,
			}, node_idx, child_idx, bb);
			
			// Loop again to insert the second partition into the parent.
			// 'bb' is updated by partition.
			child_idx = part_idx;
		} else {
			// Root node cannot be split and must be handled specially.
			u16 root_idx = tree->root = rtree_new_node(tree);
			tree->n_leaf[root_idx] = false;
			tree->n_count[root_idx] = 2;
			tree->n_child[root_idx].arr[0] = node_idx;
			tree->n_child[root_idx].arr[1] = part_idx;
			
			partition1(&(PartitionContext){
				.tree = tree, .idx0 = node_idx, .idx1 = part_idx,
				.bb_ptr[0] = tree->n_bound[root_idx].arr + 0,
				.bb_ptr[1] = tree->n_bound[root_idx].arr + 1,
			}, node_idx, child_idx, bb);
			break;
		}
	}
}

static DriftAABB2 subtree_update(DriftRTree* tree, u16 node_idx, DriftAABB2* bound, unsigned bound_count, DRIFT_ARRAY(u16)* queue){
	u16* n_child = tree->n_child[node_idx].arr;
	DriftAABB2* n_bound = tree->n_bound[node_idx].arr;
	uint count = tree->n_count[node_idx];
	
	DriftAABB2 bb = {INFINITY, INFINITY, -INFINITY, -INFINITY};
	if(tree->n_leaf[node_idx]){
		for(uint i = 0; i < count;){
			u16 idx = n_child[i];
			if(idx < bound_count && DriftAABB2Contains(n_bound[i], bound[idx])){
				bb = DriftAABB2Merge(bb, n_bound[i++]);
			} else {
				tree->n_count[node_idx] = --count;
				n_child[i] = n_child[count];
				n_bound[i] = n_bound[count];
				
				// Queue for reinsertion if index is still valid.
				if(idx < bound_count) DRIFT_ARRAY_PUSH(*queue, idx);
			}
		}
	} else {
		for(uint i = 0; i < count;){
			u16 child_idx = n_child[i];
			DriftAABB2 sub_bb = subtree_update(tree, child_idx, bound, bound_count, queue);
			if(tree->n_count[child_idx]){
				n_bound[i++] = sub_bb;
				bb = DriftAABB2Merge(bb, sub_bb);
			} else {
				tree->n_count[node_idx] = --count;
				n_child[i] = n_child[count];
				n_bound[i] = n_bound[count];
				tree->pool_arr[tree->pool_idx++] = child_idx;
			}
		}
	}
	
	return bb;
}

void DriftRTreeUpdate(DriftRTree* tree, DriftAABB2* bound, DriftAABB2* loose_bound, uint count, DriftMem* mem){
	TracyCZoneN(ZONE_UPDATE, "RTree Update", true);
	DRIFT_ARRAY(u16) queue = DRIFT_ARRAY_NEW(mem, 1024, u16);
	TracyCZoneN(ZONE_UPDATE_SUBTREES, "Update Subtrees", true);
	subtree_update(tree, tree->root, bound, count, &queue);
	TracyCZoneEnd(ZONE_UPDATE_SUBTREES);
	
	// Reset the root if it's become empty.
	if(tree->n_count[tree->root] == 0) tree->n_leaf[tree->root] = true;
	
	// Reinsert objects with invalidated bounds.
	TracyCZoneN(ZONE_REINSERT, "Reinsert Stale", true);
	unsigned queue_count = DriftArrayLength(queue);
	for(uint i = 0; i < queue_count; i++) rtree_insert(tree, queue[i], loose_bound[queue[i]]);
	TracyCZoneEnd(ZONE_REINSERT);
	
	// Finally insert untracked bounds.
	TracyCZoneN(ZONE_INSERT, "Insert New", true);
	for(uint i = tree->count; i < count; i++) rtree_insert(tree, i, loose_bound[i]);
	tree->count = count;
	TracyCZoneEnd(ZONE_INSERT);
	TracyCZoneEnd(ZONE_UPDATE);
}

static void process_leaves(DriftRTree* tree, DriftAABB2* bounds, DRIFT_ARRAY(DriftIndexPair) in_pairs, DRIFT_ARRAY(DriftIndexPair)* out_pairs){
	TracyCZoneN(ZONE_LEAVES, "Process Leaves", true);
	uint pair_count = DriftArrayLength(in_pairs);
	for(uint i = 0; i < pair_count; i++){
		DriftIndexPair pair = in_pairs[i];
		for(uint i = 0; i < tree->n_count[pair.idx0]; i++){
			uint j0 = (pair.idx0 == pair.idx1 ? i + 1 : 0);
			for(uint j = j0; j < tree->n_count[pair.idx1]; j++){
				DriftIndexPair tmp = {tree->n_child[pair.idx0].arr[i], tree->n_child[pair.idx1].arr[j]};
				if(DriftAABB2Overlap(bounds[tmp.idx0], bounds[tmp.idx1])) DRIFT_ARRAY_PUSH(*out_pairs, tmp);
			}
		}
	}
	TracyCZoneEnd(ZONE_LEAVES);
}

typedef struct LeafJobContext {
	DriftRTree* tree;
	DriftAABB2* bounds;
	DRIFT_ARRAY(DriftIndexPair) pairs;
	DRIFT_ARRAY(DriftIndexPair) result;
	
	struct LeafJobContext* next;
} LeafJobContext;

static void process_leaves_job(tina_job* job){
	LeafJobContext* ctx = tina_job_get_description(job)->user_data;
	process_leaves(ctx->tree, ctx->bounds, ctx->pairs, &ctx->result);
}

DRIFT_ARRAY(DriftIndexPair) DriftRTreePairs(DriftRTree* tree, DriftAABB2* bounds, tina_job* job, DriftMem* mem){
	TracyCZoneN(ZONE_PAIRS, "DriftRTree Pairs", true);
	DriftIndexPair root_pair = {tree->root, tree->root};
	tina_scheduler* sched = tina_job_get_scheduler(job);
	DRIFT_ARRAY(DriftIndexPair) out_pairs = DRIFT_ARRAY_NEW(mem, 3*tree->count/2, DriftIndexPair);

	// Allocate a queue to put colliding node pairs into.
	DRIFT_ARRAY(DriftIndexPair) queue_arr = DRIFT_ARRAY_NEW(mem, 512, DriftIndexPair);
	DRIFT_ARRAY_PUSH(queue_arr, root_pair);
	uint queue_tail = 0;
	
	if(tree->n_leaf[tree->root]){
		// Special case to handle the root when it's a leaf.
		process_leaves(tree, bounds, queue_arr, &out_pairs);
		TracyCZoneEnd(ZONE_PAIRS);
		return out_pairs;
	}
	
	// Use a separate queue for leaf-leaf collisions as they need to be handled specially.
	size_t leaf_count = 32;
	DriftIndexPair* leaf_queue = DRIFT_ARRAY_NEW(mem, leaf_count, DriftIndexPair);
	tina_group leaf_group = {};
	LeafJobContext* leaf_ctx = NULL;
	
	// Perform a BFS traversal of the tree.
	while(queue_tail != DriftArrayLength(queue_arr)){
		DriftIndexPair pair = queue_arr[queue_tail++];
		
		for(uint i = 0; i < tree->n_count[pair.idx0]; i++){
			// Adjust the secondary index for self colliding nodes to avoid duplicate pairs.
			uint j0 = (pair.idx0 == pair.idx1 ? i + 0 : 0);
			for(uint j = j0; j < tree->n_count[pair.idx1]; j++){
				if(DriftAABB2Overlap(tree->n_bound[pair.idx0].arr[i], tree->n_bound[pair.idx1].arr[j])){
					DriftIndexPair overlapping_pair = {tree->n_child[pair.idx0].arr[i], tree->n_child[pair.idx1].arr[j]};
					
					if(tree->n_leaf[overlapping_pair.idx0] && tree->n_leaf[overlapping_pair.idx1]){
						// Leaf-leaf collisions produce collision pairs and must be handled separately.
						if(DriftArrayLength(leaf_queue) == leaf_count){
							// Leaf queue is full. Start a job to process them.
							leaf_ctx = DRIFT_COPY(mem, ((LeafJobContext){
								.tree = tree, .bounds = bounds, .pairs = leaf_queue, .next = leaf_ctx,
								.result = DRIFT_ARRAY_NEW(mem, leaf_count*RTREE_BRANCH_FACTOR/4, DriftIndexPair),
							}));
							tina_scheduler_enqueue(sched, process_leaves_job, leaf_ctx, 0, DRIFT_JOB_QUEUE_WORK, &leaf_group);
							
							leaf_queue = DRIFT_ARRAY_NEW(mem, leaf_count, DriftIndexPair);
						}
						
						// Push the pair into the leaf queue.
						DRIFT_ARRAY_PUSH(leaf_queue, overlapping_pair);
					} else {
						// Push the pair into the regular queue.
						DRIFT_ARRAY_PUSH(queue_arr, overlapping_pair);
					}
				}
			}
		}
	}
	
	// Process any remaining leaf pairs.
	process_leaves(tree, bounds, leaf_queue, &out_pairs);
	
	TracyCZoneN(ZONE_WAIT, "Wait for Pairs", true);
	tina_job_wait(job, &leaf_group, 0);
	TracyCZoneEnd(ZONE_WAIT);
	
	// Copy results from the leaf jobs 
	TracyCZoneN(ZONE_COPY, "Copy pairs", true);
	for(LeafJobContext* ctx = leaf_ctx; ctx; ctx = ctx->next){
		size_t count = DriftArrayLength(ctx->result);
		DriftIndexPair* dst = DRIFT_ARRAY_RANGE(out_pairs, count);
		memcpy(dst, ctx->result, count*sizeof(*ctx->result));
		DriftArrayRangeCommit(out_pairs, dst + count);
	}
	TracyCZoneEnd(ZONE_COPY);

	TracyCZoneEnd(ZONE_PAIRS);
	return out_pairs;
}

#if DRIFT_DEBUG
static bool intersection(DriftAABB2 bb, DriftVec2 origin, DriftVec2 dir){
	DriftVec2 idir = {1/dir.x + FLT_MIN, 1/dir.y};
	
	float tx1 = (bb.l - origin.x)*idir.x;
	float tx2 = (bb.r - origin.x)*idir.x;

	float tmin = fminf(tx1, tx2);
	float tmax = fmaxf(tx1, tx2);

	float ty1 = (bb.b - origin.y)*idir.y;
	float ty2 = (bb.t - origin.y)*idir.y;

	tmin = fmaxf(tmin, fminf(ty1, ty2));
	tmax = fminf(tmax, fmaxf(ty1, ty2));
	DRIFT_LOG("%f %F", tmin, tmax);
	return tmax >= fmaxf(0, tmin);
}

void unit_test_rtree(void){
	DriftAABB2 bb = {-1, -1, 1, 1};
	intersection(bb, (DriftVec2){-2,  0}, (DriftVec2){1, 0});
	intersection(bb, (DriftVec2){-2, +1}, (DriftVec2){1, 0});
	intersection(bb, (DriftVec2){-2, -1}, (DriftVec2){1, 0});
	
	DRIFT_LOG("R-Tree tests passed.");
}
#endif
