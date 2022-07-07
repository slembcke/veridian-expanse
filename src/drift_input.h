typedef struct DriftInputIcon {
	const char* label;
	uint frame, advance;
} DriftInputIcon;

// If DriftInputIcon.label is the following value,
// Then the icon is part of a sequence started by the previous entry.
extern const char* DRIFT_INPUT_ICON_MULTI;

typedef enum {
	DRIFT_INPUT_SET_TYPE_MOUSE_KEYBOARD,
	DRIFT_INPUT_SET_TYPE_XBOX,
	DRIFT_INPUT_SET_TYPE_PLAYSTATION,
	_DRIFT_INPUT_SET_TYPE_COUNT,
} DriftInputIconSetType;

extern const DriftInputIcon* DRIFT_INPUT_ICON_SETS[_DRIFT_INPUT_SET_TYPE_COUNT];

const DriftInputIcon* DriftInputIconFind(const DriftInputIcon* icons, const char* label);

enum {
	DRIFT_INPUT_AXIS_MOVE_X,
	DRIFT_INPUT_AXIS_MOVE_Y,
	DRIFT_INPUT_AXIS_LOOK_X,
	DRIFT_INPUT_AXIS_LOOK_Y,
	DRIFT_INPUT_AXIS_ACTION1,
	DRIFT_INPUT_AXIS_ACTION2,
	_DRIFT_INPUT_AXIS_COUNT,
	
	DRIFT_INPUT_ACCEPT = 0x0001,
	DRIFT_INPUT_CANCEL = 0x0002,
	DRIFT_INPUT_TOGGLE_HEADLIGHT = 0x0004,
	DRIFT_INPUT_OPEN_UI = 0x0008,
	DRIFT_INPUT_ACTION1 = 0x0010,
	DRIFT_INPUT_ACTION2 = 0x0020,
	DRIFT_INPUT_CARGO_PREV = 0x0040,
	DRIFT_INPUT_CARGO_NEXT = 0x0080,
	DRIFT_INPUT_QUICK_SLOT1 = 0x1000,
	DRIFT_INPUT_QUICK_SLOT2 = 0x2000,
	DRIFT_INPUT_QUICK_SLOT3 = 0x4000,
	DRIFT_INPUT_QUICK_SLOT4 = 0x8000,
	DRIFT_INPUT_QUICK_SLOTS = 0xF000,
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
	
	bool ui_active;
} DriftPlayerInput;

typedef struct {
	DriftPlayerInput player;
	
	DriftVec2 mouse_pos_clip, mouse_pos;
	DriftVec2 mouse_rel_clip, mouse_rel;
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
	return (len < 0.25f ? DRIFT_VEC2_ZERO : (len < 1 ? v : DriftVec2Mul(v, 1/len)));
}

typedef struct DriftGameContext DriftGameContext;
typedef struct DriftNuklear DriftNuklear;

// TODO this one's a real mouthful.
void DriftInputEventsPoll(DriftApp* app, DriftGameContext* ctx, tina_job* job, DriftNuklear* nk, DriftAffine vp_inverse);
