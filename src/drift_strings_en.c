/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include "drift_game.h"
#include "drift_strings.h"

#define DELAY "{+60}"

// This is sarcastic gold, but where to use it...
// Instead of solving our own problems, we chose to explore outer space and make new ones.

const char* DRIFT_STRINGS[] = {

// Intro strings.

// HOMEWORLD IMAGE

[DRIFT_STRING_INTRO_TROUBLE] =
"The homeworld is in trouble." DELAY " We thought it would last forever, and so we ignored its true value until it was almost used up.",

[DRIFT_STRING_INTRO_SOLUTION] =
"The energy crisis was solved eventually, but it was too late. Every year there is less green. We have technology to purify the air and water, but the dwindling food supply is making us more reliant on the colonies.",


// COLONY IMAGE

[DRIFT_STRING_INTRO_COLONIES] =
"To be fair, the colonies almost worked." DELAY " Corporate farming communities popped up across a dozen habitable worlds. The frontier gave settlers a clean start, and they thrived in their new lives.",

[DRIFT_STRING_INTRO_PROBLEM] =
"Unfortunately, no matter how fast production grows, the homeworld's biosphere is failing even faster." DELAY " That's when we started working on plan C.",

// SCIENCE IMAGE

[DRIFT_STRING_INTRO_PROPOSALS] =
"The best minds came together to offer solutions. There was a flurry of proposals including geo, bio, and social engineering projects on scales the world had never seen.",

[DRIFT_STRING_INTRO_REJECTED] =
"One by one they were all rejected." DELAY " We couldn't afford them!" DELAY " Besides, why spend generations fixing a dirty, broken world when you can rush order a brand new one at half the price?",

// VIRIDIUS IMAGE

[DRIFT_STRING_INTRO_VIRIDIUM] =
"In order to sustain terraforming, we needed to unlock planetary scale industrial fabrication technology, and a recent discovery in the Viridius system was the key. Viridius Betus was even a perfect candidate to transform!",

[DRIFT_STRING_INTRO_BETUS] =
"The deed to the system was held by the Planetary Pioneers Corporation. Initially, the investors nearly rioted when they saw how much it cost. A distant system with only a single, barely habitable planet? Now they were salivating!",

// PIONEER IMAGE

[DRIFT_STRING_INTRO_PIONEER] =
"With all the planning complete, the Pioneer was launched to get everything bootstrapped. You'd think the company would send their best, but your last performance review wasn't exactly a novel:" DELAY " \"a competent pilot\".",

[DRIFT_STRING_INTRO_BABYSIT] =
"Honestly, Eida will be doing most of the work anyway with the cold, boring precision that only a machine can tolerate. The skeleton crew is really just there to babysit a computer.",

[DRIFT_STRING_INTRO_INTERESTING] =
"While drifting off into hypersleep before the jump, you think: \"Even if the job is going to be boring, at least it will be easy...\"",


// Tutorial strings

[DRIFT_STRING_TUT_SEISMIC] =
"Pod 9, are you receiving? Seismic activity has been detected in your vicinity. Please report any damage to company property or bodily injury. Pod 9 your response is required.",

[DRIFT_STRING_TUT_WAKE] =
"Aurgh... my head is pounding."DELAY" Eida? I'm okay I think..."DELAY" I don't remember waking from hypersleep though. How did I get in a pod? Is this a cave?!",

[DRIFT_STRING_TUT_CHECKLIST] =
"Pod 9, your comm signal is very weak. Standard procedure dictates following the provided equipment test checklist. First, test your thrusters. " HIGHLIGHT "Use {@MOVE} and {@LOOK} to find your power nework.",

[DRIFT_STRING_TUT_THRUSTERS] =
"Thrusters are working, but only micro-gravity is being detected. WHAT THE CRAP ARE THOSE?! Eida, I'm not alone here. This... is not Viridius Betus is it? Eida, are you recieving?!",

[DRIFT_STRING_TUT_ACK] =
"Pod 9 comms acknowledged. Your accident should have minimal impact on the repair schedule. Does this give you relief? However, please hold your questions pending completion of the equipment checklist.",

[DRIFT_STRING_TUT_RECONNECT] =
"How unfortunate. You've allowed your power network to become damaged. " HIGHLIGHT "Use those extra power nodes to reconnect your fabricator.",

[DRIFT_STRING_TUT_STASH] =
HIGHLIGHT "Use {@LOOK} to pull the node in" DRIFT_TEXT_WHITE " and stash it in your cargo hold..",

[DRIFT_STRING_TUT_RECONNECT2] =
"Adequate. Now pick up another power node.",

[DRIFT_STRING_TUT_RECONNECT3] =
"Those nodes are sufficient to "HIGHLIGHT"reconnect the fabricator"DRIFT_TEXT_WHITE". Then a diagnostic self test can be initiated.",

[DRIFT_STRING_TUT_SCANNER] =
"Adequate. To proceed " HIGHLIGHT "scan your fabricator using {@SCAN} and {@LOOK}" DRIFT_TEXT_WHITE " to check its status.",

[DRIFT_STRING_TUT_SCANNER2] =
"The scanning beam is also a full duplex data tranciever. "HIGHLIGHT"Press {@SCAN} again to connect to your fabricator.",

[DRIFT_STRING_TUT_CORRUPT] =
"Your ability to delay the schedule was underestimated. You've managed to corrupt both your pod's " HIGHLIGHT "scan database" DRIFT_TEXT_WHITE " and the fabricator's " HIGHLIGHT "blueprint database"DRIFT_TEXT_WHITE".",

[DRIFT_STRING_TUT_RESTORE] =
"Thanks for that Eida... Just restore the databases from the backups on the Pioneer, and connect me with Captain Jansdottr. If you aren't going to tell me what's going on then I need to talk to someone else.",

[DRIFT_STRING_TUT_UNAVAILABLE] =
"Repairs to shipboard systems such as backups and comms cannot be completed with your current status. First, you must restore your databases by...",

[DRIFT_STRING_TUT_HAPPENED] =
"Eida stop... What happened to the ship, and what is this place?",

[DRIFT_STRING_TUT_DATABASES] =
"Please hold your questions. Those databases must be restored first. So " HIGHLIGHT "scan items with the scanner and research blueprints at the fabricator.",

[DRIFT_STRING_TUT_LUMIUM] =
HIGHLIGHT"Lumium can be obtained by destroying glow bugs."DRIFT_TEXT_WHITE" Make sure to scan other creatures to discover more sources of raw materials.",

[DRIFT_STRING_TUT_SCANS] =
"That should be enough scans to restore a missing blueprint. "HIGHLIGHT"Research lumium lights at your fabricator.",

	// wait_for_message(script, ACCEPT_TIMEOUT, MESSAGE_WRENCHY, "Adequate. You've proven capable of basic autonomy despite your injuries. As you continue recovering your pod's functions, your personal impact on the Pioneer's repairs will be reduced.");
	// wait_for_message(script, ACCEPT_TIMEOUT, MESSAGE_PLAYER, "How...{+60} thoughtful, Eida.{+60} Now will you tell me what happened to the ship? Where are we, and what are these lifeforms? The Viridius system wasn't supposed to have anything like this in it.");
	// wait_for_message(script, ACCEPT_TIMEOUT, MESSAGE_WRENCHY, "Departing from Viridius Primus, the Pioneer struck a transport craft. Its course was deflected out into the Viridian Expanse where it collided with asteroid GL330. The impact destroyed primary systems including propulsion.");
	// wait_for_message(script, ACCEPT_TIMEOUT, MESSAGE_WRENCHY, "GL330 hosts a self sustaining ecosystem of bio-mechanoid lifeforms. Reparing the ship is paramount, as Planetary Pioneers Corporation will want a patent on this Viridium based biochemistry.");
	// wait_for_message(script, ACCEPT_TIMEOUT, MESSAGE_PLAYER, "Ah yes...{+60} patents. I can't think of anything more important except teaching you about sarcasm. Anyway, I really think it's time I talk to Captain Jansdottr.");
	// show_message(script, MESSAGE_WRENCHY, "The rest of the crew is currently unavailable. However, sensors are detecting a rich metallic ore deposit near your position. Further investigation is warranted. " HIGHLIGHT "Press {@MAP} view it on your map.");

};
