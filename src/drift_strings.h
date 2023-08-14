/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

typedef enum {
	DRIFT_STRING_INTRO_TROUBLE,
	DRIFT_STRING_INTRO_SOLUTION,
	DRIFT_STRING_INTRO_COLONIES,
	DRIFT_STRING_INTRO_PROBLEM,
	DRIFT_STRING_INTRO_PROPOSALS,
	DRIFT_STRING_INTRO_REJECTED,
	DRIFT_STRING_INTRO_VIRIDIUM,
	DRIFT_STRING_INTRO_BETUS,
	DRIFT_STRING_INTRO_PIONEER,
	DRIFT_STRING_INTRO_BABYSIT,
	DRIFT_STRING_INTRO_INTERESTING,

	DRIFT_STRING_TUT_SEISMIC,
	DRIFT_STRING_TUT_WAKE,
	DRIFT_STRING_TUT_CHECKLIST,
	DRIFT_STRING_TUT_THRUSTERS,
	DRIFT_STRING_TUT_ACK,
	DRIFT_STRING_TUT_RECONNECT,
	DRIFT_STRING_TUT_STASH,
	DRIFT_STRING_TUT_RECONNECT2,
	DRIFT_STRING_TUT_RECONNECT3,
	DRIFT_STRING_TUT_SCANNER,
	DRIFT_STRING_TUT_SCANNER2,
	DRIFT_STRING_TUT_CORRUPT,
	DRIFT_STRING_TUT_RESTORE,
	DRIFT_STRING_TUT_UNAVAILABLE,
	DRIFT_STRING_TUT_HAPPENED,
	DRIFT_STRING_TUT_DATABASES,
	DRIFT_STRING_TUT_LUMIUM,
	DRIFT_STRING_TUT_SCANS,
} DriftStringID;

extern const char* DRIFT_STRINGS[];

#define HIGHLIGHT "{#00C0C0FF}"
// #define TEXT_ACCEPT_PROMPT "\n{#808080FF}Press {@ACCEPT} to continue..."
