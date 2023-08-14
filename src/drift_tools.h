/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

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
