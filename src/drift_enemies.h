typedef enum {
	DRIFT_ENEMY_NONE,
	DRIFT_ENEMY_GLOW_BUG,
	DRIFT_ENEMY_WORKER_BUG,
	DRIFT_ENEMY_FIGHTER_BUG,
	_DRIFT_ENEMY_COUNT,
} DriftEnemyType;

extern DriftCollisionCallback DriftWorkerDroneCollide;

void DriftTickEnemies(DriftUpdate* update);
void DriftDrawEnemies(DriftDraw* draw);
