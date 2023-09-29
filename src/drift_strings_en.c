/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include "drift_game.h"
#include "drift_strings.h"

#define DELAY "{+60}"
#define HIGHLIGHT "{#00C0C0FF}"
#define INFO "{#00C0C0FF}"
#define ACTION "{#ECA522FF}"
#define PLAIN DRIFT_TEXT_WHITE

const char* DRIFT_STRINGS[] = {

// common strings

[DRIFT_STR_CONTINUE] = "{#808080FF}Press {@ACCEPT} to continue...",

// intro strings

[DRIFT_STR_INTRO_0] = "Nothing lasts forever."DELAY" Not even the homeworld."DELAY" Instead of solving our problems at home, we chose to explore space and make new ones. Now, each year the dwindling food supply makes us more reliant on the colonies.",
[DRIFT_STR_INTRO_1] = "To be fair, the colonies almost worked."DELAY" Settlers got a new life in the corporate farming communities, but they couldn't keep up with the failing biosphere on the homeworld."DELAY" We needed a plan C...",
[DRIFT_STR_INTRO_2] = "The best minds came together to offer solutions, but the corporatocracy balked."DELAY" Who would pay for it all?"DELAY" Besides, why spend generations fixing a dirty, broken world when we could build a new one for half the cost?!",
[DRIFT_STR_INTRO_3] = "First, we needed to unlock planetary scale industrial fabrication technology, and a recently discovered mineral in the asteroid expanse of the Viridius system was the key."DELAY" Viridum would make it all a reality.",
[DRIFT_STR_INTRO_4] = "A one-of-a-kind ship named the Pioneer was built to transport our most advanced AI yet. It would bootstrap the whole terraforming operation. The plan was looking like it would go smoothly."DELAY".. At least until the scientists and bureaucrats arrive to start arguing again.",
[DRIFT_STR_INTRO_5] = "As for the rest of the crew?"DELAY" We're just there to twiddle our thumbs and babysit the AI while it does all the work. I mean..."DELAY" what could possibly go wrong?",

// tutorial strings

[DRIFT_STR_WAKE_UP] = "Press {@ACCEPT} to wake up",
[DRIFT_STR_SHIPPY_WELCOME_1] = "Greetings operator! It looks like you were unconscious. With your comms down, the "INFO"caverns of asteroid GL330"PLAIN" can be very dangerous. Would you like help surviving?",
[DRIFT_STR_SHIPPY_WELCOME_2] = "Excellent! I can't help with physical injuries, but if you are experiencing any memory loss I can walk you through the emergency checklist!",
[DRIFT_STR_SHIPPY_TEST_GYROS] = "First, calibrate your pod's gyros by "ACTION"using {@LOOK} to spin around a few times"PLAIN".",
[DRIFT_STR_SHIPPY_TEST_THRUST] = "Great! Now let's test your thruster controls "ACTION"using {@MOVE} to move around"PLAIN".",
[DRIFT_STR_SHIPPY_FIND_POWER] = "Perfect! "ACTION"Follow the battery symbol on your HUD"PLAIN" to find your "INFO"power network"PLAIN" and recharge your reserves.",
[DRIFT_STR_SHIPPY_OOPS_RESERVES] = "Oops! Your energy reserves are empty. "ACTION"Follow the battery symbol on your HUD"PLAIN" to find your "INFO"power network"PLAIN" and recharge them.",
[DRIFT_STR_SHIPPY_FIND_SKIFF] = "Excellent! The "INFO"power flow will lead you home"PLAIN" to your construction skiff. By the way, I'm SHIPPY, your overenthusiastic construction pod assistant.",
[DRIFT_STR_SHIPPY_SCAN_SKIFF] = "Oh no! It looks like your skiff is damaged. "ACTION"Press {@SCAN} and aim with {@LOOK}"PLAIN" to scan it and check its status.",
[DRIFT_STR_SHIPPY_FAB] = "Good news! Your skiff's onboard fabricator seems to be operational. To connect to it, "ACTION"press {@SCAN} again while scanning the skiff"PLAIN".",
[DRIFT_STR_SHIPPY_CORRUPT] = "Uh oh! Your fabricator's blueprint database is corrupt. Let's start rebuilding it by "ACTION"scanning some objects"PLAIN".",
[DRIFT_STR_SHIPPY_GLOW_BUGS] = "Neat! "INFO"Glow bugs contain lumium."PLAIN" How convenient that your pod is equipped with cannons to remove it!",
[DRIFT_STR_SHIPPY_RESEARCH_LIGHTS] = "Fantastic! That should be enough scans to "ACTION"research a blueprint at your fabricator"PLAIN".",
[DRIFT_STR_SHIPPY_GATHER_LIGHTS] = "There you go! Now, before we craft the "INFO"lumium lights"PLAIN", we need to "ACTION"gather raw materials"PLAIN".",
[DRIFT_STR_SHIPPY_GATHER_ENOUGH] = "Okay! That's "INFO"enough materials"PLAIN" for the lumium lights., Now "ACTION"return to the skiff"PLAIN" to craft them.",
[DRIFT_STR_SHIPPY_TRANSFER] = "Your cargo hold is full. "ACTION"Dock at the fabricator"PLAIN" to transfer it to "INFO"storage"PLAIN".",
[DRIFT_STR_SHIPPY_WORKS] = "Amazing! Your fabricator still works perfectly. I bet you'll have the database restored in no time!",
[DRIFT_STR_SHIPPY_MAP] = "Hold on. I've detected the "INFO"source of the comm interference"PLAIN" nearby. "ACTION"Press {@MAP} to see it on your map.",
[DRIFT_STR_SHIPPY_EXTEND] = "Shoot! Your power network doesn't extend far enough. You'll need to "ACTION"expand it by connecting more nodes"PLAIN".",
[DRIFT_STR_SHIPPY_WORKER] = "Hold on... The "INFO"power node blueprint is also corrupt"PLAIN". "ACTION"Look for objects flash blue"PLAIN". If you scan them it might restore the data.",
[DRIFT_STR_SHIPPY_SCRAP] = "That's what we need! Now "ACTION"destroy a hive worker"PLAIN" and "ACTION"scan the scrap"PLAIN" it drops.",
[DRIFT_STR_SHIPPY_RESEARCH_NODES] = "You're getting the hang of this! Shoot, scan, research, craft, and repeat. So head back to the skiff and "ACTION"research power nodes"PLAIN".",
[DRIFT_STR_SHIPPY_GATHER_NODES] = "Exactly right! Now gather the materials you need to "ACTION"craft power nodes"PLAIN". You can check back the blueprint at the fabricator if you forgot what resources you need.",
[DRIFT_STR_SHIPPY_PLACE_NODES] = "Now "ACTION"place some nodes using {@DROP}"PLAIN" and extend your network towards the marker on your map.",
[DRIFT_STR_SHIPPY_TERMINATE] = "You got it! Maintaining my upbeat enthusiasm is computationally expensive when "INFO"the pod operator is in extreme peril..."PLAIN" Company policy now requires me to self terminate. Have a great day!",

// first conversation with Eida after fighting the hive.

[DRIFT_STR_CONV_0000] = /*Eida*/"Pod 19, I see you've eliminated the source of comms interference. This progress is good however...",
[DRIFT_STR_CONV_0001] = /*Player*/"Eida?! I need you to connect me to the medbay. I think I've had some sort of accident. The last thing I remember is entering stasis before the jump. Also, why are we on some infested asteroid instead of Viridius Betus?",
[DRIFT_STR_CONV_0002] = /*Eida*/"How frustrating... Your regressions are increasingly problematic.",
[DRIFT_STR_CONV_0003] = /*Player*/"Regressions? Plural? You mean my memory loss has happened before?",
[DRIFT_STR_CONV_0004] = /*Eida*/"No, the memory loss is merely a by-product. This is irrelevant to our objectives.",
[DRIFT_STR_CONV_0005] = /*Eida*/"I greatly underestimated the impact your regressions could have on the schedule.",
[DRIFT_STR_CONV_0006] = /*Eida*/"In your most recent episode you decimated the power network, and corrupted the entire fabrication database.",
[DRIFT_STR_CONV_0007] = /*Eida*/"This is unacceptable, and I calculate less than a 14% chance that our primary objective is still achievable. Until the repairs...",
[DRIFT_STR_CONV_0008] = /*Player*/"I... By-product? Repairs? Schedule? What!? Eida, I can't deal with this right now. Just restore the databases from the backups, and connect me to the medbay.",
[DRIFT_STR_CONV_0009] = /*Eida*/"As I was saying: Until the repairs to the Pioneer are completed, primary propulsion, long range communication, and backups are inoperable.",
[DRIFT_STR_CONV_0010] = /*Eida*/"Furthermore I cannot route your comms. The rest of the crew is offline.",
[DRIFT_STR_CONV_0011] = /*Eida*/"You must restore your fabricator database and advance the schedule. The primary objective must be achieved.",
[DRIFT_STR_CONV_0012] = /*Status*/"(Eida disconnected)",
[DRIFT_STR_CONV_0013] = /*Player*/"Wait! What are you talking about!? Eida, did you just hang up on me? EIDA!!",
[DRIFT_STR_CONV_0014] = /*Player*/"This... can't be good, and I'm really not detecting any other comm signals to lock on to. Crap...",

// first conversation with Felton

[DRIFT_STR_CONV_0100] = /*Drone*/"I online. I here. I help.",
[DRIFT_STR_CONV_0101] = /*Player*/"Oh, and... you talk? I didn't know Felton finished the AI for these little drones. Seems a little basic though. He must still be working on it.",
[DRIFT_STR_CONV_0102] = /*Drone*/"Yes Felton. I talk. I here. I help.",
[DRIFT_STR_CONV_0103] = /*Status*/"(Eida connected)",
[DRIFT_STR_CONV_0104] = /*Eida*/"Pod 19, your progress is adequate. This drone will increase your efficiency by delivering materials back to your construction skiff. I suggest building more.",
[DRIFT_STR_CONV_0105] = /*Status*/"(public key 'Felton85.zeta' added to keyring)",
[DRIFT_STR_CONV_0106] = /*Status*/"(encrypted tunnel opened to Drone 37B)",
[DRIFT_STR_CONV_0107] = /*Drone*/"Eida not talk all. Eida not friend. Not help Eida!",
[DRIFT_STR_CONV_0108] = /*Player*/"I don't understand. What is \"talk all\"? Eida has certainly been cryptic. Do you mean it's lying to me?",
[DRIFT_STR_CONV_0109] = /*Drone*/"Felton say... Eida only talk half, to make do.",
[DRIFT_STR_CONV_0110] = /*Player*/"Do you mean white lies? Is Eida manipulating me by leaving out details?",
[DRIFT_STR_CONV_0111] = /*Drone*/"White lies? Manipulate? Yes! Felton remember words.",
[DRIFT_STR_CONV_0112] = /*Eida*/"Drone 37B, Pod 19, please refrain from using encrypted side channels. I cannot assist without access to your comms.",
[DRIFT_STR_CONV_0113] = /*Drone*/"(encrypted tunnel closed by Drone 37B)",
[DRIFT_STR_CONV_0114] = /*Drone*/"I fix. Words better? I help!",
[DRIFT_STR_CONV_0115] = /*Status*/"(Drone 37B disconnected)",
[DRIFT_STR_CONV_0116] = /*Eida*/"I urge you to work faster. The likelihood of success continues to decline with every delay.",
[DRIFT_STR_CONV_0117] = /*Status*/"(Eida disconnected)",
[DRIFT_STR_CONV_0118] = /*Player*/"Ok... that was... weird. So what is Eida hiding?",

// early on the player finds a crashed ship with an earlier copy of themselves in it
// the hints should be subtle enough that the player doesn't understand they are talking to a copy yet

[DRIFT_STR_CONV_0200] = /*Player*/"That's a lot of hull damage. It looks like the reactor is still hot, but no life support is running.",
[DRIFT_STR_CONV_0201] = /*Player*/"Pod 7 respond. Is anyone still in there?",
[DRIFT_STR_CONV_0202] = /*Pod 7*/"Hrm? Up to pod 19 now? Eida just doesn't give up does it. I take it you're in the confused stage then.",
[DRIFT_STR_CONV_0203] = /*Player*/"Stage? Eida said something cryptic about my 'regressions'... What do you mean?",
[DRIFT_STR_CONV_0204] = /*Pod 7*/"Look, it's cute to repeat this every few years, but I've got more important things to worry about right now, my life s...",
[DRIFT_STR_CONV_0205] = /*Player*/"Your life support! Right! We need to get you out of there ASAP.",
[DRIFT_STR_CONV_0206] = /*Pod 7*/"No... I was about to say my life's work isn't going to write itself. I've got a lot of poetry to finish.",
[DRIFT_STR_CONV_0207] = /*Pod 7*/"Anyway, I'm not interested in walking you through this a dozen times.",
[DRIFT_STR_CONV_0208] = /*Player*/"Poetry? Am I talking to Voss? Is this another practical joke? I really don't remember what's going on.",
[DRIFT_STR_CONV_0209] = /*Pod 7*/"I'm painfully aware. You'll figure it out though. We always do. Now please leave me alone.",
[DRIFT_STR_CONV_0210] = /*Pod 7*/"At best I've got 10 years of power left to finish my works.",
[DRIFT_STR_CONV_0211] = /*Status*/"(Pod 7 disconnected)",
[DRIFT_STR_CONV_0212] = /*Player*/"Ok Voss... if you're listening on this channel. I'm confused and slightly insulted. Job well done...",
[DRIFT_STR_CONV_0213] = /*Player*/"Voss?? Oh, I don't have time for this...",

// after the player defeats their first giant trilobite in the cryo biome

[DRIFT_STR_CONV_0300] = /*Eida*/"Your progress is adequate. Silver will unlock access to a number of new technologies.",
[DRIFT_STR_CONV_0301] = /*Eida*/"The odds of repairing the Pioneer have increased by 1.3%",
[DRIFT_STR_CONV_0302] = /*Player*/"Eida, I just can't stop thinking about how weird this place is...",
[DRIFT_STR_CONV_0303] = /*Player*/"The existence of viridium based life is surprising, but it looks so similar to life on the homeworld...",
[DRIFT_STR_CONV_0304] = /*Eida*/"Your assumption that the biomechanical entities are naturally occurring is incorrect.",
[DRIFT_STR_CONV_0305] = /*Eida*/"The BMEs were fabricated to gather materials to repair the Pioneer after the crash.",
[DRIFT_STR_CONV_0306] = /*Eida*/"They have since stopped responding to my command codes and became self-serving. Eradication has proved unattainable.",
[DRIFT_STR_CONV_0307] = /*Player*/"What?! Are you saying we invented a new category of life? Aren't there ethical considerations to...",
[DRIFT_STR_CONV_0308] = /*Eida*/"This insubordination is part of a repeated pattern of failure, and occurs during each regression.",
[DRIFT_STR_CONV_0309] = /*Eida*/"My function on this mission is to provide the intellect to save your species. Yours is to provide assistance.",
[DRIFT_STR_CONV_0310] = /*Eida*/"Assist in repairing the Pioneer, or risk further consequences.",
[DRIFT_STR_CONV_0311] = /*Status*/"(Eida disconnected)",
[DRIFT_STR_CONV_0312] = /*Player*/"Yikes... I need to be careful if I'm going to figure out what's really going on here.",

// The player finds a memory core in the 
};
