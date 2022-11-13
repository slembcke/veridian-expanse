typedef enum {
	DRIFT_TOOL_NONE,
	DRIFT_TOOL_GRAB,
	DRIFT_TOOL_SCAN,
	DRIFT_TOOL_DIG,
	DRIFT_TOOL_GUN,
	_DRIFT_TOOL_COUNT,
} DriftToolType;

typedef struct DriftPlayerData DriftPlayerData;

typedef void DriftToolUpdateFunc(DriftUpdate* update, struct DriftPlayerData* player, DriftAffine transform);
typedef void DriftToolDrawFunc(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform);

void DriftToolUpdate(DriftUpdate* update, struct DriftPlayerData* player, DriftAffine transform);
void DriftToolDraw(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform);
