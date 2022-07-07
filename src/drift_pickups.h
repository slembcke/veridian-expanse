typedef struct DriftUpdate DriftUpdate;

typedef void DriftPickupFunc(DriftUpdate* update, DriftEntity e, DriftVec2 pos);
typedef void DriftPickupDrawFunc(DriftDraw* draw, DriftVec2 pos);
typedef DriftEntity DriftPickupMakeFunc(DriftGameState* state, DriftVec2 pos, DriftVec2 vel);

typedef struct {
	DriftPickupFunc* grab;
	DriftPickupFunc* drop;
	DriftPickupDrawFunc* draw;
	DriftPickupMakeFunc* make;
} DriftPickupItem;

extern const DriftPickupItem DRIFT_PICKUPS[_DRIFT_ITEM_TYPE_COUNT];

DriftEntity DriftPickupMake(DriftGameState* state, DriftVec2 pos, DriftVec2 vel, DriftItemType type);
