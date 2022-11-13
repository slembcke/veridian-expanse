#pragma once

#include "base/drift_base.h"

typedef struct DriftGameContext DriftGameContext;
typedef struct DriftGameState DriftGameState;
typedef struct DriftUpdate DriftUpdate;
typedef struct DriftDraw DriftDraw;

#include "drift_constants.h"
#include "drift_sprite.h"
#include "drift_draw.h"
#include "drift_terrain.h"
#include "drift_sdf.h"
#include "drift_physics.h"
#include "drift_tools.h"
#include "drift_input.h"
#include "drift_scan.h"
#include "drift_items.h"
#include "drift_ui.h"
#include "drift_enemies.h"
#include "drift_systems.h"
#include "drift_game_context.h"

#if DRIFT_DEBUG
extern DriftVec4 TMP_COLOR[4];
extern float TMP_VALUE[4];
#endif
