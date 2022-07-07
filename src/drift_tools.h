typedef enum {
	DRIFT_TOOL_FLY,
	DRIFT_TOOL_GRAB,
	DRIFT_TOOL_DIG,
	DRIFT_TOOL_GUN,
	_DRIFT_TOOL_COUNT,
} DriftToolType;

typedef struct DriftUpdate DriftUpdate;
typedef struct DriftPlayerData DriftPlayerData;

typedef void DriftToolUpdateFunc(DriftUpdate* update, struct DriftPlayerData* player, DriftAffine transform);
typedef void DriftToolDrawFunc(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform);

typedef struct {
	const char* name;
	DriftToolUpdateFunc* update;
	DriftToolDrawFunc* draw;
} DriftTool;

extern const DriftTool DRIFT_TOOLS[_DRIFT_TOOL_COUNT];
