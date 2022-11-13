typedef struct DriftInputIcon {
	const char* label;
	uint frame, advance;
} DriftInputIcon;

// If DriftInputIcon.label is the following value,
// Then the icon is part of a sequence started by the previous entry.
extern const char* DRIFT_INPUT_ICON_MULTI;

typedef enum {
	DRIFT_INPUT_SET_MOUSE_KEYBOARD,
	DRIFT_INPUT_SET_XBOX,
	DRIFT_INPUT_SET_PLAYSTATION,
	_DRIFT_INPUT_SET_COUNT,
} DriftInputIconSetType;

extern const DriftInputIcon* DRIFT_INPUT_ICON_SETS[_DRIFT_INPUT_SET_COUNT];

const DriftInputIcon* DriftInputIconFind(const DriftInputIcon* icons, const char* label);

enum {
	DRIFT_INPUT_AXIS_MOVE_X,
	DRIFT_INPUT_AXIS_MOVE_Y,
	DRIFT_INPUT_AXIS_LOOK_X,
	DRIFT_INPUT_AXIS_LOOK_Y,
	DRIFT_INPUT_AXIS_FIRE,
	DRIFT_INPUT_AXIS_ALT,
	_DRIFT_INPUT_AXIS_COUNT,
	
	DRIFT_INPUT_ACCEPT = (1<<0),
	DRIFT_INPUT_CANCEL = (1<<1),
	DRIFT_INPUT_PAUSE = (1<<2),
	DRIFT_INPUT_OPEN_MAP = (1<<3),
	DRIFT_INPUT_PREV = (1<<4),
	DRIFT_INPUT_NEXT = (1<<5),
	
	DRIFT_INPUT_LIGHT = (1<<6),
	DRIFT_INPUT_FIRE = (1<<7),
	DRIFT_INPUT_ALT = (1<<8),
	DRIFT_INPUT_GRAB = (1<<9),
	DRIFT_INPUT_SCAN = (1<<10),
	DRIFT_INPUT_DROP = (1<<11),
	DRIFT_INPUT_LASER = (1<<12),
};

enum {
	DRIFT_MOUSE_LEFT,
	DRIFT_MOUSE_MIDDLE,
	DRIFT_MOUSE_RIGHT,
	_DRIFT_MOUSE_COUNT,
};

typedef struct {
	float axes[_DRIFT_INPUT_AXIS_COUNT];
	u64 bstate;
	u64 bpress;
	u64 brelease;
	
	float _digital_axis[_DRIFT_INPUT_AXIS_COUNT];
	float _analog_axis[_DRIFT_INPUT_AXIS_COUNT];
} DriftPlayerInput;

typedef struct {
	DriftPlayerInput player;
	
	DriftVec2 mouse_pos_clip, mouse_pos_world, mouse_rel;
	bool mouse_up[_DRIFT_MOUSE_COUNT], mouse_down[_DRIFT_MOUSE_COUNT], mouse_state[_DRIFT_MOUSE_COUNT];
	float mouse_wheel;
	
	bool quit, mouse_captured;
	DriftInputIconSetType icon_type;
	
#ifdef DRIFT_MODULES
	bool request_hotload;
#endif
} DriftInput;

static inline bool DriftInputButtonState(DriftPlayerInput* player, u64 mask){return (player->bstate & mask) != 0;}
static inline bool DriftInputButtonPress(DriftPlayerInput* player, u64 mask){return (player->bpress & mask) != 0;}
static inline bool DriftInputButtonRelease(DriftPlayerInput* player, u64 mask){return (player->brelease & mask) != 0;}

static inline DriftVec2 DriftInputJoystick(DriftPlayerInput* player, uint x, uint y){
	DriftVec2 v = {player->axes[x], -player->axes[y]};
	float len = DriftVec2Length(v);
	// TODO hardcoded deadzone
	return (len < 0.15f ? DRIFT_VEC2_ZERO : (len < 1 ? v : DriftVec2Mul(v, 1/len)));
}

typedef struct DriftGameContext DriftGameContext;
typedef struct DriftNuklear DriftNuklear;
typedef struct mu_Context mu_Context;

void DriftInputEventsPoll(DriftGameContext* ctx, DriftAffine vp_inverse);
