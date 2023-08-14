/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include <limits.h>
#include <string.h>

#include <SDL.h>

#include "drift_base.h"

#include "tracy/TracyC.h"

static float k_metric(DriftVec2 a, DriftVec2 b){return DriftVec2LengthSq(DriftVec2Sub(a, b));}
static u64 k_means2(DriftAABB2 bound[], uint count){
	// Find the extreme points.
	DriftVec2 c = DriftAABB2Center(bound[0]);
	DriftVec2 min_x = c, min_y = c, max_x = c, max_y = c;
	for(uint i = 1; i < count; i++){
		DriftVec2 c = DriftAABB2Center(bound[i]);
		if(c.x < min_x.x) min_x = c;
		if(c.x > max_x.x) max_x = c;
		if(c.y < min_y.y) min_y = c;
		if(c.y > max_y.y) max_y = c;
	}
	
	// Use the longest axis as a heuristic to prime the k-means.
	DriftVec2 mean[2];
	if(max_x.x - min_x.x > max_y.y - min_y.y){
		mean[0] = min_x, mean[1] = max_x;
	} else{
		mean[0] = min_y, mean[1] = max_y;
	}
	
	u64 partition_bits = 0;
	for(uint loop = 0; loop < 4; loop++){
		DRIFT_ASSERT(!(mean[0].x == mean[1].x && mean[0].y == mean[1].y), "Matching means.");
		partition_bits = 0;
		
		DriftVec2 sum[2] = {};
		float weight[2] = {};
		for(uint i = 0; i < count; i++){
			DriftAABB2 bb = bound[i];
			float area = DriftAABB2Area(bb);
			DRIFT_ASSERT(area > 0, "Empty bound.");
			DriftVec2 c = DriftAABB2Center(bb);
			if(k_metric(c, mean[0]) < k_metric(c, mean[1])){
				sum[0] = DriftVec2FMA(sum[0], c, area), weight[0] += area;
			} else {
				sum[1] = DriftVec2FMA(sum[1], c, area), weight[1] += area;
				partition_bits |= 1 << i;
			}
		}
		
		mean[0] = DriftVec2Mul(sum[0], 1/weight[0]);
		mean[1] = DriftVec2Mul(sum[1], 1/weight[1]);
		
		DRIFT_ASSERT(isfinite(mean[0].x*mean[0].y*mean[1].x*mean[1].y), "Non-finite means.");
		DRIFT_ASSERT(partition_bits != 0 && partition_bits + 1 != 1ull << count, "Failed to partition node.");
	}
	
	return partition_bits;
}

static DriftAABB2 rnode_bound(DriftRNode* node){
	DriftAABB2 bb = node->bb0[0];
	for(uint i = 1; i < node->count; i++) bb = DriftAABB2Merge(bb, node->bb0[i]);
	return bb;
}

static bool rnode_insert(DriftRNode* node, uint child_idx, DriftAABB2 child_bb){
	if(node->count < DRIFT_RTREE_BRANCH_FACTOR){
		node->child[node->count] = child_idx;
		node->bb0[node->count] = node->bb1[node->count] = child_bb;
		node->count++;
		return true;
	} else {
		return false;
	}
}

static void rnode_remove(DriftRNode* node, uint idx){
	DRIFT_ASSERT(node->count > 0, "Node is already empty.");
	uint last_idx = --node->count;
	node->child[idx] = node->child[last_idx];
	node->bb0[idx] = node->bb0[last_idx];
	node->bb1[idx] = node->bb1[last_idx];
}

typedef struct {
	DriftRNode* node[2];
	DriftAABB2* bb_ptr[2];
} PartitionContext;

static void partition_overflow(PartitionContext ctx, uint child_idx, DriftAABB2 bb){
	DRIFT_ASSERT(ctx.node[0] != ctx.node[1], "Non-unique nodes for partitioning");
	
	// Copy
	DriftAABB2 bound[2*DRIFT_RTREE_BRANCH_FACTOR] = {bb};
	memcpy(bound + 1, ctx.node[0]->bb0, DRIFT_RTREE_BRANCH_FACTOR*sizeof(*bound));
	uint child[2*DRIFT_RTREE_BRANCH_FACTOR] = {child_idx};
	memcpy(child + 1, ctx.node[0]->child, DRIFT_RTREE_BRANCH_FACTOR*sizeof(*child));

	// Partition
	u64 partition_bits = k_means2(bound, DRIFT_RTREE_BRANCH_FACTOR + 1);
	
	// Insert into output nodes.
	ctx.node[0]->count = ctx.node[1]->count = 0;
	for(uint i = 0; i < DRIFT_RTREE_BRANCH_FACTOR + 1; i++){
		rnode_insert(ctx.node[partition_bits & 1], child[i], bound[i]);
		partition_bits >>= 1;
	}
	
	// Calculate bounds.
	*ctx.bb_ptr[0] = rnode_bound(ctx.node[0]);
	*ctx.bb_ptr[1] = rnode_bound(ctx.node[1]);
}

static uint rtree_new_node(DriftRTree* tree){
	// Pop a node from the pool or add a new one.
	return (tree->pool_idx ? tree->pool_arr[--tree->pool_idx] : DriftTablePushRow(&tree->t));
}

static void rtree_insert(DriftRTree* tree, uint user_idx, DriftAABB2 bb){
	uint stack_arr[8] = {tree->root};
	uint stack_count = 1;
	
	// Find leaf to insert into.
	uint node_idx = tree->root;
	for(uint depth = 0; depth < tree->leaf_depth; depth++){
		DriftRNode* node = tree->node + node_idx;
		
		float near_dist = INFINITY;
		uint near_idx = 0;
		DriftVec2 c = DriftAABB2Center(bb);
		for(uint i = 0; i < node->count; i++){
			float dist = DriftVec2LengthSq(DriftVec2Sub(c, DriftAABB2Center(node->bb0[i])));
			if(dist < near_dist) near_dist = dist, near_idx = i;
		}
		
		// Expand to include the new bb.
		node->bb0[near_idx] = DriftAABB2Merge(node->bb0[near_idx], bb);
		
		DRIFT_ASSERT_HARD(stack_count < sizeof(stack_arr)/sizeof(*stack_arr), "DriftRTree stack overflow");
		stack_arr[stack_count++] = node_idx = node->child[near_idx];
	}
	
	// Walk back up the tree, inserting and splitting as needed.
	uint child_idx = user_idx;
	while(stack_count){
		uint node_idx = stack_arr[--stack_count];
		if(rnode_insert(tree->node + node_idx, child_idx, bb)) break;
		
		// Insertion failed. Need to split the node by partitioning the children.
		uint split_idx = rtree_new_node(tree);
		
		if(stack_count){
			// Split the node in two.
			uint parent_idx = stack_arr[stack_count - 1];
			
			uint bound_idx = 0;
			while(tree->node[parent_idx].child[bound_idx] != node_idx) bound_idx++;
			
			partition_overflow((PartitionContext){
				.node = {tree->node + node_idx, tree->node + split_idx},
				.bb_ptr = {tree->node[parent_idx].bb0 + bound_idx, &bb},
			}, child_idx, bb);
			
			// Loop again to insert the second partition into the parent.
			// 'bb' is updated by partition.
			child_idx = split_idx;
		} else {
			// Special case to grow the tree when the root splits.
			tree->root = rtree_new_node(tree);
			tree->node[tree->root] = (DriftRNode){.count = 2, .child = {node_idx, split_idx}};
			tree->leaf_depth++;
			
			partition_overflow((PartitionContext){
				.node = {tree->node + node_idx, tree->node + split_idx},
				.bb_ptr = {tree->node[tree->root].bb0 + 0, tree->node[tree->root].bb0 + 1},
			}, child_idx, bb);
		}
	}
}

typedef struct {
	DriftRTree* tree; DriftRNode* nodes;
	uint obj_count;
	DriftRTreeBoundFunc* bound_func; void* bound_data;
	DriftAABB2* leaf_bounds;
	
	tina_scheduler* sched;
	tina_group leaf_group;
	DRIFT_ARRAY(uint) leaf_batch;
	
	SDL_mutex* reinsert_lock;
	DRIFT_ARRAY(uint) reinsert_queue;
} TreeUpdateCtx;

static DriftAABB2 update_leaf(TreeUpdateCtx* ctx, DriftRNode* node){
	// Remove children with invalid indexes.
	for(uint i = 0; i < node->count;){
		if(node->child[i] < ctx->obj_count) i++; else rnode_remove(node, i);
	}
	
	// Update bounds.
	ctx->bound_func(node->child, node->bb1, node->count, ctx->bound_data);
	
	// Update leaf.
	DriftAABB2 node_bb = {INFINITY, INFINITY, -INFINITY, -INFINITY};
	for(int i = node->count - 1; i >= 0; i--){
		DriftAABB2 obj_bb = node->bb1[i];
		DriftAABB2 loose_bb = DriftAABB2Merge(node->bb0[i], obj_bb);
		// Check loose/obj bound ratio to decide to reinsert or not.
		if(DriftAABB2Area(loose_bb) < 8*DriftAABB2Area(obj_bb)){
			node->bb0[i] = loose_bb;
			node_bb = DriftAABB2Merge(node_bb, obj_bb);
		} else {
			SDL_LockMutex(ctx->reinsert_lock);
			DRIFT_ARRAY_PUSH(ctx->reinsert_queue, node->child[i]);
			SDL_UnlockMutex(ctx->reinsert_lock);
			rnode_remove(node, i);
		}
	}
	
	return node_bb;
}

typedef struct {
	TreeUpdateCtx* ctx;
	DRIFT_ARRAY(uint) indexes;
} LeafTask;

static void leaf_job(tina_job* job){
	TracyCZoneN(ZONE, "Update Leaves", true);
	LeafTask* task = tina_job_get_description(job)->user_data;
	TreeUpdateCtx* ctx = task->ctx;
	DRIFT_ARRAY_FOREACH(task->indexes, idx_ptr) ctx->leaf_bounds[*idx_ptr] = update_leaf(ctx, ctx->nodes + *idx_ptr);
	TracyCZoneEnd(ZONE);
}

static void leaf_update_rec(TreeUpdateCtx* ctx, DriftRNode* node, uint depth){
	if(depth == 1){
		for(uint i = 0; i < node->count; i++){
			DriftArray* header = DriftArrayHeader(ctx->leaf_batch);
			ctx->leaf_batch[header->count++] = node->child[i];
			if(header->count == header->capacity){
				tina_scheduler_enqueue(ctx->sched, leaf_job, DRIFT_COPY(header->mem, ((LeafTask){
					.ctx = ctx, .indexes = ctx->leaf_batch,
				})), 0, DRIFT_JOB_QUEUE_WORK, &ctx->leaf_group);
				ctx->leaf_batch = DRIFT_ARRAY_NEW(header->mem, header->capacity, uint);
			}
		}
	} else {
		DriftRNode* nodes = ctx->nodes;
		for(uint i = 0; i < node->count; i++) leaf_update_rec(ctx, nodes + node->child[i], depth - 1);
	}
}

static DriftAABB2 node_update_rec(TreeUpdateCtx* ctx, DriftRNode* node, uint depth){
	DriftAABB2 node_bb = {INFINITY, INFINITY, -INFINITY, -INFINITY};
	for(int i = node->count - 1; i >= 0; i--){
		uint child_idx = node->child[i];
		DriftAABB2 child_bb = (depth > 1 ? node_update_rec(ctx, ctx->nodes + child_idx, depth - 1) : ctx->leaf_bounds[child_idx]);
		if(ctx->nodes[child_idx].count){
			node->bb0[i] = child_bb;
			node_bb = DriftAABB2Merge(node_bb, child_bb);
		} else {
			ctx->tree->pool_arr[ctx->tree->pool_idx++] = child_idx;
			rnode_remove(node, i);
		}
	}
	
	return node_bb;
}

void DriftRTreeUpdate(DriftRTree* tree, uint obj_count, DriftRTreeBoundFunc bound_func, void* bound_data, tina_job* job, DriftMem* mem){
	TracyCZoneN(ZONE_UPDATE, "RTree Update", true);
	TreeUpdateCtx ctx = {
		.tree = tree, .nodes = tree->node, .obj_count = obj_count,
		.bound_func = bound_func, .bound_data = bound_data,
		.leaf_bounds = DRIFT_ARRAY_NEW(mem, tree->t.row_count, DriftAABB2),
		.sched = tina_job_get_scheduler(job),
		.leaf_batch = DRIFT_ARRAY_NEW(mem, 32, uint),
		.reinsert_queue = DRIFT_ARRAY_NEW(mem, obj_count/4, uint),
	};
	
	DriftRNode* root = tree->node + tree->root;
	
	// TODO This should really move into the tree?
	ctx.reinsert_lock = SDL_CreateMutex();
	uint depth = tree->leaf_depth;
	if(depth > 0){
		TracyCZoneN(ZONE_UPDATE_LEAVES, "Update Leaves", true);
		leaf_update_rec(&ctx, root, tree->leaf_depth);
		DRIFT_ARRAY_FOREACH(ctx.leaf_batch, idx) ctx.leaf_bounds[*idx] = update_leaf(&ctx, tree->node + *idx);
		tina_job_wait(job, &ctx.leaf_group, 0);
		TracyCZoneEnd(ZONE_UPDATE_LEAVES);
		
		TracyCZoneN(ZONE_UPDATE_NODES, "Update Nodes", true);
		node_update_rec(&ctx, root, tree->leaf_depth);
		TracyCZoneEnd(ZONE_UPDATE_NODES);
	} else {
		// Special case when the root is a leaf.
		update_leaf(&ctx, root);
	}
	SDL_DestroyMutex(ctx.reinsert_lock);
	
	// Reset the tree if it's become empty.
	if(root->count == 0) tree->leaf_depth = 0;
	
	// Reinsert objects with invalidated bounds.
	TracyCZoneN(ZONE_REINSERT, "Reinsert Stale", true);
	unsigned queue_count = DriftArrayLength(ctx.reinsert_queue);
	DRIFT_ARRAY_FOREACH(ctx.reinsert_queue, idx_ptr){
		DriftAABB2 bb; bound_func(idx_ptr, &bb, 1, bound_data);
		rtree_insert(tree, *idx_ptr, bb);
	}
	TracyCZoneEnd(ZONE_REINSERT);
	
	// Finally insert new objects.
	TracyCZoneN(ZONE_INSERT, "Insert New", true);
	for(uint i = tree->count; i < obj_count; i++){
		DriftAABB2 bb; bound_func((uint[]){i}, &bb, 1, bound_data);
		rtree_insert(tree, i, bb);
	}
	tree->count = obj_count;
	TracyCZoneEnd(ZONE_INSERT);
	TracyCZoneEnd(ZONE_UPDATE);
}

static void process_leaf_pair(DriftRNode* node0, DriftRNode* node1, DRIFT_ARRAY(DriftIndexPair)* out_pairs){
	for(uint i = 0; i < node0->count; i++){
		DriftAABB2 bb0 = node0->bb1[i];
		// Adjust indexes for self colliding nodes. 
		uint j_max = (node0 == node1 ? i : node1->count);
		for(uint j = 0; j < j_max; j++){
			DriftIndexPair tmp = {node0->child[i], node1->child[j]};
			if(DriftAABB2Overlap(bb0, node1->bb1[j])) DRIFT_ARRAY_PUSH(*out_pairs, tmp);
		}
	}
}

typedef struct {DriftRNode* leaf[2];} LeafPair;

typedef struct LeafJobContext {
	DRIFT_ARRAY(DriftIndexPair) result;
	DRIFT_ARRAY(LeafPair) pairs;
	struct LeafJobContext* next;
} LeafJobContext;

static void process_leaves_job(tina_job* job){
	TracyCZoneN(ZONE_LEAVES, "Process Leaves", true);
	LeafJobContext* ctx = tina_job_get_description(job)->user_data;
	DRIFT_ARRAY_FOREACH(ctx->pairs, pair) process_leaf_pair(pair->leaf[0], pair->leaf[1], &ctx->result);
	TracyCZoneEnd(ZONE_LEAVES);
}

DRIFT_ARRAY(DriftIndexPair) DriftRTreePairs(DriftRTree* tree, tina_job* job, DriftMem* mem){
	TracyCZoneN(ZONE_PAIRS, "DriftRTree Pairs", true);
	tina_scheduler* sched = tina_job_get_scheduler(job);
	DRIFT_ARRAY(DriftIndexPair) out_pairs = DRIFT_ARRAY_NEW(mem, 3*tree->count/2, DriftIndexPair);
	
	DriftRNode* root = tree->node + tree->root;
	// Special case to handle the root when it's a leaf.
	if(tree->leaf_depth == 0){
		process_leaf_pair(root, root, &out_pairs);
		goto finish;
	}
	
	// Allocate a queue to put colliding node pairs into.
	typedef struct {DriftRNode* node[2]; uint depth;} NodePair;
	DRIFT_ARRAY(NodePair) queue_arr = DRIFT_ARRAY_NEW(mem, 512, NodePair);
	DRIFT_ARRAY_PUSH(queue_arr, ((NodePair){{root, root}, 1}));
	uint queue_tail = 0;
	
	// Use a separate queue for leaf-leaf collisions as they need to be handled specially.
	size_t leaf_count = 128;
	LeafPair* leaf_queue = DRIFT_ARRAY_NEW(mem, leaf_count, LeafPair);
	tina_group leaf_group = {};
	LeafJobContext* leaf_ctx = NULL;
	
	// Perform a BFS traversal of the tree.
	while(queue_tail != DriftArrayLength(queue_arr)){
		NodePair pair = queue_arr[queue_tail++];
		for(uint i = 0; i < pair.node[0]->count; i++){
			DriftAABB2 bb0 = pair.node[0]->bb0[i];
			uint j_max = (pair.node[0] == pair.node[1] ? i + 1 : pair.node[1]->count);
			for(uint j = 0; j < j_max; j++){
				if(DriftAABB2Overlap(bb0, pair.node[1]->bb0[j])){
					DriftRNode* child0 = tree->node + pair.node[0]->child[i];
					DriftRNode* child1 = tree->node + pair.node[1]->child[j];
					if(pair.depth == tree->leaf_depth){
						// Leaf-leaf collisions produce collision pairs and must be handled separately.
						if(DriftArrayLength(leaf_queue) == leaf_count){
							// Leaf queue is full. Start a job to process them.
							leaf_ctx = DRIFT_COPY(mem, ((LeafJobContext){
								.result = DRIFT_ARRAY_NEW(mem, leaf_count*DRIFT_RTREE_BRANCH_FACTOR/4, DriftIndexPair),
								.pairs = leaf_queue, .next = leaf_ctx,
							}));
							tina_scheduler_enqueue(sched, process_leaves_job, leaf_ctx, 0, DRIFT_JOB_QUEUE_WORK, &leaf_group);
							
							leaf_queue = DRIFT_ARRAY_NEW(mem, leaf_count, LeafPair);
						}
						
						// Push the pair into the leaf queue.
						DRIFT_ARRAY_PUSH(leaf_queue, ((LeafPair){{child0, child1}}));
					} else {
						DRIFT_ASSERT(pair.depth != tree->leaf_depth, "leaf depth");
						// Push the pair into the regular queue.
						DRIFT_ARRAY_PUSH(queue_arr, ((NodePair){{child0, child1}, pair.depth + 1}));
					}
				}
			}
		}
	}
	
	// Process any remaining leaf pairs.
	DRIFT_ARRAY_FOREACH(leaf_queue, pair) process_leaf_pair(pair->leaf[0], pair->leaf[1], &out_pairs);
	
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
	
	finish:
	TracyCZoneEnd(ZONE_PAIRS);
	return out_pairs;
}
