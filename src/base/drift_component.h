#pragma once

typedef struct DriftComponent {
	DriftTable table;
	DriftMap map;
	bool reset_on_hotload;
	
	uint count;
	uint gc_cursor;
	void (*cleanup)(struct DriftComponent* component, uint idx);
} DriftComponent;

// The first row in the component table is reserved for the entity.
// Convenience macro used to better document the behavior.
// #define DRIFT_COMPONENT_ENTITY_COLUMN (DriftColumn){0}

DriftComponent* DriftComponentInit(DriftComponent* component, DriftTableDesc desc);
void DriftComponentDestroy(DriftComponent* component);

void DriftComponentIO(DriftComponent* component, DriftIO* io);

// Add a component for the given entity and return it's index.
uint DriftComponentAdd(DriftComponent *component, DriftEntity entity);

// Delete a component for the given entity.
void DriftComponentRemove(DriftComponent* component, DriftEntity entity);

// Find the component index for a given entity.
static inline uint DriftComponentFind(DriftComponent* component, DriftEntity entity){
	return DriftMapFind(&component->map, entity.id);
}

static inline DriftEntity* DriftComponentGetEntities(DriftComponent* component){
	return component->table.desc.columns.arr[0].ptr;
}

// Clean up components for deleted entities.
// Higher values for 'pressure' cause more cleanup.
void DriftComponentGC(DriftComponent* component, DriftEntitySet* entities, uint pressure);

#define DRIFT_COMPONENT_FOREACH(_comp_, _idx_) \
	for(uint _idx_ = 1, count = (_comp_)->count; _idx_ <= count; _idx_++)

typedef void DriftJoinFunc(DriftEntity entity, void* context);

#define DRIFT_JOIN_MAX_COMPONENTS 8

typedef struct {
	uint* variable;
	DriftComponent* component;
	bool optional;
} DriftComponentJoin;

typedef struct {
	DriftEntity entity;
	DriftComponentJoin joins[DRIFT_JOIN_MAX_COMPONENTS];
	uint count;
} DriftJoin;

DriftJoin DriftJoinMake(DriftComponentJoin* joins);
bool DriftJoinNext(DriftJoin* join);
