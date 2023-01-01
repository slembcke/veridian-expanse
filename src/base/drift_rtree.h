#define RTREE_BRANCH_FACTOR 24

typedef struct {
	u16 root, count;
	uint pool_idx;
	
	DriftTable table;
	bool* n_leaf;
	u8* n_count;
	struct {DriftAABB2 arr[RTREE_BRANCH_FACTOR];}* n_bound;
	struct {u16 arr[RTREE_BRANCH_FACTOR];}* n_child;
	u16* pool_arr;
} DriftRTree;

void DriftRTreeUpdate(DriftRTree* tree, DriftAABB2* bound, DriftAABB2* loose_bound, uint count, DriftMem* mem);
DRIFT_ARRAY(DriftIndexPair) DriftRTreePairs(DriftRTree* tree, DriftAABB2* bounds, tina_job* job, DriftMem* mem);
