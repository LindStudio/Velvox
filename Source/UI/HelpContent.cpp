#include "HelpContent.h"

namespace velvox::ui::help
{
    namespace
    {
        const char* const gStart = "Getting started";
        const char* const gWork = "Working with Velvox";
        const char* const gModules = "Modules";

        // Builds a module page from its seven answers
        Page modulePage (const char* key, const char* title, const char* what, const char* when, const char* how,
                         const char* values, const char* start, const char* mistakes, const char* together)
        {
            const auto& h = moduleSectionHeadings();
            juce::String body;
            const char* parts[] { what, when, how, values, start, mistakes, together };
            for (int i = 0; i < 7; ++i)
                body << "## " << h[i] << "\n" << parts[i] << "\n\n";
            return { key, title, gModules, body.trim() };
        }

        std::vector<Page> build()
        {
            std::vector<Page> p;

            //==================================================================
            p.push_back ({ "start", "Welcome to Velvox", gStart, R"(Velvox is a vocal processing system for modern EDM, hypertechno, hard dance and hardstyle vocals. Everything a lead vocal usually needs - cleaning, dynamics, tone, width, space and rhythmic effects - sits in one chain of modules.

## Your first minute
- Pick a preset from the field at the top centre. The Punchy and Aggressive presets are good starting points for hard dance leads.
- Play your vocal. The input meter on the left should sit roughly around -20 LUFS; use Input gain if it is much quieter or clips.
- In Beginner mode, shape the sound with the six large controls. Rest the mouse on one for a second to read what it does.
- Press Bypass (top right) to hear the original, and A / B to compare two versions.

## Two ways to work
- Beginner: six musical controls, a live spectrum and live tips.
- Pro: every module with all of its real parameters, meters and visualisations.
Both modes control the same sound. Switching never changes or resets a value.

## Help is always here
The Help button at the top right opens this guide. Inside a module, the ? button jumps straight to the page for that module. You can restart the tutorial from the bottom of the navigation.)" });

            p.push_back ({ "chain", "The signal chain", gStart, R"(The row under the top bar is the signal chain. Audio enters at In, runs through the modules from left to right and leaves at Out.

## Reading a tile
- The dot on the left is the power switch. Lit = the module processes audio, hollow = it is switched off and bypassed.
- The dot glows brighter while the module is working hard (for example while a compressor reduces gain).
- An "M" on a switched-off tile means a Beginner control currently drives this module.
- Delay and Reverb carry an Ins / Send switch: Ins processes in series like every other module, Send runs them in parallel on the lower lane.

## Why order matters
Every module hears what the modules before it did. Typical consequences:
- Level Ride before the Compressor gives the compressor an even input, so it can work gently.
- Saturation after the De-esser does not exaggerate S sounds; before it, the de-esser also tames the harmonics saturation created.
- Reverb and Delay at the end (or as sends) keep the effects from being compressed and saturated.
- Vocal Check analyses the signal at its own position. Put it last to judge the result, first to judge the recording.
The default order follows these rules and is a safe place to start.)" });

            p.push_back ({ "modules", "Organising modules", gWork, R"(## Open a module
Click a tile to open it. In Pro mode its controls appear below the chain. In Beginner mode you see a short description and can jump to Pro to edit it.

## Switch on and off
Click the dot on a tile, or the power button in the module header. A switched-off module is bypassed: it is removed from the active processing and costs no CPU.

## Change the order
Drag a tile left or right by its body. A glowing marker shows where it will land. Release to drop - the audio order changes at the same moment (with a short, click-free crossfade). Drag Delay or Reverb onto the lower lane to run them as parallel sends, or back into the row to insert them.
Dragging starts only after the mouse has moved a few pixels, so a normal click never moves a module.

## Hide modules you do not use
Right-click a tile and choose "Hide from chain view", or use the Modules button below the chain. Hiding only tidies the view: a hidden module keeps its settings and keeps processing if it is switched on. The Modules button shows how many modules are hidden and how many of those are still active. Use it to open, switch or show any hidden module again.

## Hide, switch off or remove - the difference
- Hidden: not shown in the row. The sound does not change.
- Switched off: shown with a hollow dot. The module is not processed.
- Sends: Delay and Reverb on the lower lane are not in the series chain but are added in parallel.

## Layouts
In the Modules menu:
- Restore default order puts every module back to its factory position.
- Save as my default layout stores the current order and hidden modules for you (in your user settings, for all projects).
- Load my default layout applies that saved layout to this instance.
The order and hidden modules of each instance are also saved with your project and in presets (order only).)" });

            p.push_back ({ "controls", "Controls and values", gWork, R"(## Knobs
- Drag up or right to raise a value, down or left to lower it.
- Hold Shift (or Ctrl) while dragging for fine adjustment.
- Double-click a knob (or Alt-click) to reset it to its default value.
- Click the value under a knob to type an exact number. Units are understood: "2k" or "2000" for 2 kHz.
- The mouse wheel also moves a knob.
- Rest the mouse on a knob for a second to see what it does.

## Switches, menus and buttons
- Pill switches show a lit dot when on.
- Menus show the current choice; click to choose another.
- Hover over any switch or button to see a short tooltip.

## Automation
Every knob, switch and menu is a real plugin parameter: your DAW can automate it, and presets and A/B store it. Velvox reports changes to the host as proper gestures, so recorded automation is clean.)" });

            p.push_back ({ "modes", "Beginner and Pro", gWork, R"(## Beginner
Six large controls change several modules at once in a musical way:
- Presence: more high-end air on the EQ, and a little more de-essing to keep the extra S sounds in check.
- Warmth: more low-end body, slightly softer highs and gentle saturation.
- Aggression: heavier and faster compression with more drive.
- Width: doubled voices left and right and a wider stereo image. The centre stays untouched.
- Space: reverb and delay that duck while you sing.
- Density: more compression and a touch of saturation for a steady, thick vocal.
The Fair compare menu sets how Bypass compares (see Compare: Bypass and A/B).

## Pro
Every module with all its parameters. When a Beginner control moves a parameter, the knob in Pro shows a small teal marker at the value that is actually processed.

## Switching
Beginner and Pro are two views of the same settings. Switching mode never changes the sound or resets anything: the Beginner controls stay active in Pro, and the Pro settings stay underneath in Beginner.)" });

            p.push_back ({ "assistant", "Assistant", gWork, R"(## What it does
Describe the sound you want in your own words - in German or English - and the Assistant proposes concrete changes to the Velvox modules. It runs completely inside the plugin: no internet, no cloud, no language model.

## How to use it
- Open the Assistant tab at the top (next to Beginner and Pro).
- Type your wish, e.g. "weniger Tiefen", "etwas mehr Reverb", "mach die Stimme präsenter, aber nicht schärfer". Several wishes in one sentence are fine.
- Enter (or Analysieren) builds the change list; Shift+Enter starts a new line.
- Every proposal shows the module, the parameter, the current and the proposed value and why. Untick what you do not want, then click Vorschläge anwenden.
- Rückgängig restores the values from before the last Assistant action. Values you changed yourself in the meantime are kept.

## What it understands
Low end, presence, highs, reverb, delay, level, stereo width, warmth, harshness, S sounds, punch, compression, and "the vocal should cut through the beat". Words like etwas / ein wenig / deutlich / stark set how far it goes. "Nicht schärfer" or "nicht mehr Höhen" are read as limits, never as the opposite wish. Vague or contradictory requests get a question instead of a guess.

## Measurements
- With VOCAL CHECK on and enough voice analysed, proposals quote the measured balance of the voice and are tagged Messung.
- The beat sidechain is used for a level comparison only. A frequency comparison of vocal and beat (masking) is not implemented yet - the Assistant says so instead of pretending.

## Good to know
The Assistant only moves the normal module parameters. Everything stays visible in the modules, works with presets, A/B and automation, and nothing is stored separately.)" });

            p.push_back ({ "presets", "Presets and saving", gWork, R"(## Factory presets
Click the preset field at the top centre to open the browser. Factory presets are grouped by character (Clean, Punchy, Aggressive, Wide, Creative, ...); your own presets are listed separately under User presets. The arrows left and right step through all presets.

## Edited presets
As soon as you change a setting, the preset field shows "edited". Load the preset again to go back, or save your version.

## Your own presets
- Save stores every setting and the module order as a user preset. Factory presets are never overwritten.
- Delete removes the selected user preset file (factory presets cannot be deleted).
- User presets are files in your Documents folder (Lind Audio/Velvox/Presets), so you can back them up or share them.

## Module presets
The Presets button inside a module loads starting points for that module only; the rest of the chain stays as it is.

## What presets do not touch
Input gain, Bypass and the Fair compare / Level match settings belong to your recording and monitoring, so presets and A/B never change them.

## Projects
Your DAW project stores the complete state of every Velvox instance: all parameters, the module order, the hidden modules, the view mode and the window size.)" });

            p.push_back ({ "compare", "Compare: Bypass and A/B", gWork, R"(## Bypass
Bypass (top right) plays the unprocessed vocal, delay-compensated so it stays in time. While bypass is on, Velvox shows an amber frame and a BYPASSED label so you never forget it. It is also the host's bypass parameter.

## Fair comparison
A louder sound almost always seems better. Set Fair compare (Beginner) or Level match (Pro, Output) to:
- Bypass match: Bypass plays as loud as the processed vocal, so you compare tone, not loudness.
- Auto gain: the processed vocal is continuously kept as loud as the input.
- Off: no level adjustment.
Level matching never happens silently: it only works when you choose it.

## A / B
A and B are two complete sets of settings. Work on A, press Copy A to B, change B, then switch between A and B to compare. Switching restores every parameter and the module order of that slot.)" });

            p.push_back ({ "meters", "Meters and displays", gWork, R"(## Input and Output
- The bars show the peak level of left and right, with a hold line.
- The number below is the loudness in LUFS (short term).
- The red lamp on the output lights when the signal went above 0 dBFS. Click it to reset.
- Clip guard: a soft clipper from -1 dBFS that keeps the output below 0 dBFS.
- Mono: listen in mono to check compatibility (not saved with the project).

## Correlation
The small bar on the output rail shows the phase correlation. +1 is mono-safe, around 0 is very wide, below 0 parts of the sound cancel in mono.

## Module displays
Each module shows real data from the audio or from its own settings: the EQ curve over the live spectrum, the compressor's gain reduction, the de-esser band, the doubler and choir voices, the pulse curve with the gain that is applied, the level ride history and more. Nothing on screen is simulated.)" });

            p.push_back ({ "problems", "Fixing common vocal problems", gWork, R"(- Rumble, plosives, muddy low end: EQ Low cut around 80 to 120 Hz. For plosives that trigger the compressor, raise the Compressor's SC filter.
- Boxy or honky: EQ Mid, cut 2 to 4 dB between 300 and 800 Hz with a moderate Q.
- Harsh, ringing resonances: Smooth with moderate Amount; narrow, sharp spots: EQ Mid with a high Q.
- Sharp S and T sounds: De-esser, find the band with Listen, then set Threshold so only the S sounds trigger.
- Uneven words, some disappear: Level Ride before the Compressor.
- Not loud or dense enough: Vocal Smash blended under the vocal, then the Compressor for control.
- Too dry or too far back: Reverb and Delay as sends with ducking, so the words stay clear.
- Too narrow: Doubler or Choir, then check Mono on the output rail.
- Clipping output: lower Output gain and keep Clip guard on.
- Not sure where to start: switch on Vocal Check at the end of the chain and play the vocal for a while. Click a finding to open the module it recommends.)" });

            //==================================================================
            p.push_back (modulePage ("eq", "Equalizer",
                "Shapes the tone of the voice: a low cut against rumble, a low band for body, a mid bell for boxiness or presence, a high band for air and a high cut for darker or lo-fi sounds. The curve is drawn over the live spectrum.",
                "Almost always, as the first cleaning step. Use it to remove what you do not want and to give the vocal its basic colour.",
                "- Switch on Low cut and set its Frequency so the rumble below the voice disappears.\n- Low band, Mid and High band each have Freq and Gain; Q sets the width of a bell (and the shape of a shelf).\n- Type switches the low and high band between Shelf (everything beyond the frequency) and Bell (only around it).\n- High cut removes the very top, e.g. for telephone effects.\n- Mix blends the equalized and the original signal.",
                "- Gain: positive values boost, negative values cut (+/-18 dB).\n- Q: higher = narrower bell, lower = broad and musical.\n- Low cut Frequency: higher removes more low end; too high makes the voice thin.\n- Slope: 24 dB/oct cuts more steeply than 12 dB/oct.",
                "- Low cut 80 to 120 Hz, 24 dB/oct.\n- Mid: -2 to -4 dB around 400 Hz if the voice sounds boxy.\n- High band: +2 to +4 dB shelf at 10 kHz for air.",
                "- Boosting many bands instead of cutting the problem.\n- Very narrow, large boosts that start to ring.\n- Setting the low cut so high that the voice loses its body.\n- Judging only by the curve - always listen.",
                "Put the EQ before the Compressor so the compressor does not react to rumble. Large high boosts can make S sounds sharper; follow with the De-esser. Smooth catches resonances that move, which a static EQ cannot."));

            p.push_back (modulePage ("comp", "Compressor",
                "Reduces the level of loud passages so that the vocal stays steady and every word sits in the mix. The meter on the right shows the gain reduction in dB.",
                "On nearly every lead vocal. Use it for control and consistency; use Vocal Smash for extra density.",
                "- Threshold sets where compression starts; Ratio how strongly it compresses above that level.\n- Attack is how fast it reacts, Release how fast it lets go.\n- Knee softens the transition around the threshold.\n- Makeup adds level back after compression; Mix blends in the uncompressed signal (parallel compression).\n- Detector: Peak reacts to every transient, RMS follows the average level.\n- SC filter keeps low frequencies (plosives, bass) from triggering the compressor.",
                "- Lower Threshold or higher Ratio = more gain reduction.\n- Attack: shorter times catch transients faster but can dull consonants; longer times let the attack of words through.\n- Release: short = denser and more audible pumping, long = smoother but can hold the level down.\n- Mix below 100 % keeps some natural dynamics.",
                "- Ratio 3:1 to 4:1, Attack 5 to 15 ms, Release 80 to 150 ms.\n- Lower the Threshold until the meter shows about 3 to 6 dB reduction on loud words.\n- Use Makeup to match the bypassed loudness.",
                "- Too much reduction (over 10 dB permanently) - the vocal sounds flat and breathy.\n- Very short attack on an already hard vocal.\n- Comparing with Bypass at different loudness - use Fair compare.",
                "Level Ride before the Compressor evens out whole words so the compressor can work gently. The De-esser after it catches S sounds the compressor made louder. Vocal Smash adds a parallel squashed layer on top."));

            p.push_back (modulePage ("deess", "De-esser",
                "Reduces sharp S, T and SH sounds. It detects energy above a set frequency and lowers it only while it is too loud.",
                "When S sounds hiss or cut, especially after EQ boosts, compression or saturation.",
                "- Frequency sets the lower edge of the band that is reduced.\n- Threshold sets the level where the reduction starts.\n- Range limits how much the band may be reduced.\n- Mode: Split band only reduces the S frequencies; Wideband dips the whole voice on each S.\n- Listen plays only the detected band, to find the right Frequency.",
                "- Lower Threshold = reacts to more and quieter S sounds.\n- Higher Range = stronger reduction.\n- Frequency: lower values also catch lower hiss but can make the voice lisp.",
                "- Frequency 5 to 7 kHz, Range 6 dB.\n- Turn on Listen, move Frequency until you hear mostly S sounds, turn Listen off.\n- Lower Threshold until only the S sounds are reduced.",
                "- Too much Range: the singer sounds as if they had a lisp.\n- Leaving Listen switched on (you only hear the S band).\n- Using Wideband on a dense mix where the whole vocal then ducks audibly.",
                "Place it after EQ boosts and compression, which both make S sounds louder. Presence in Beginner mode raises the de-essing automatically together with the highs. Smooth handles ringing resonances, the de-esser handles sibilance."));

            p.push_back (modulePage ("sat", "Saturation",
                "Adds harmonics: warmth, grit, density and a stronger presence in a loud mix. The drive is level compensated, so it changes character rather than volume.",
                "When the vocal sounds too clean, thin or polite for an energetic track.",
                "- Character: Warm (smooth, slightly asymmetric), Tape (gentle, symmetric), Tube (rich even harmonics), Hard (firm clipping).\n- Drive sets the amount of saturation.\n- Output trims the level after the stage.\n- Mix blends the saturated and the clean signal.",
                "- More Drive = more harmonics and more audible distortion.\n- Lower Mix = subtler, keeps the clean transients.\n- Hard character distorts most at the same Drive.",
                "- Warm or Tape, Drive 4 to 8 dB, Mix 100 %.\n- For aggressive leads: Hard with Mix 30 to 50 %.",
                "- Too much drive on S sounds - put the De-esser before it or lower Drive.\n- Judging distortion only on solo; in the mix more is often fine, but check.",
                "Saturation increases density similar to compression; with Vocal Smash or heavy compression use less. Warmth, Aggression and Density in Beginner mode add saturation automatically."));

            p.push_back (modulePage ("doubler", "Doubler",
                "Creates two extra takes left and right with small timing and pitch differences, for a wide and thick vocal while the original stays in the centre.",
                "For choruses, hooks and adlibs that should feel bigger and wider.",
                "- Mix sets the level of the doubled voices; the original stays at full level.\n- Width places the voices left and right.\n- Timing sets the delay of the doubles (longer feels looser).\n- Detune sets their pitch variation.\n- Mono safe keeps the doubles out of the mono sum.\n- Tone darkens the doubles so they sit behind the lead.",
                "- Higher Mix = more obvious doubling.\n- More Detune and Timing = looser, more chorus-like; too much sounds out of tune or echoey.\n- Mono safe 100 % = mono playback sounds exactly like the original.",
                "- Mix 40 to 50 %, Width 80 %, Timing 12 to 18 ms, Detune 6 to 10 ct, Mono safe 50 to 100 %.",
                "- Too much Detune on a tuned lead.\n- Forgetting the club: check with Mono on the output rail.",
                "Choir is the bigger brother for whole groups of voices; you rarely need both at full level. Width in Beginner mode drives the doubler. Put reverb after the doubler so both doubles get the same room."));

            p.push_back (modulePage ("pitch", "Pitch",
                "Creative transposition: shifts the vocal by semitones and cents, with optional formant control. It is not a pitch correction tool.",
                "For octave layers, chipmunk hooks, dark monster voices and effects.",
                "- Pitch transposes in semitones, Fine in cents.\n- Keep formants keeps the natural character of the voice; off moves the character with the pitch.\n- Formant moves the character independently of the pitch.\n- Mix blends the shifted voice with the original (for layers).",
                "- Large shifts sound more artificial, especially downwards.\n- Formant up = smaller, brighter voice; down = bigger, darker voice.\n- Mix 50 % = octave layer; 100 % = only the shifted voice.",
                "- Octave layer: Pitch -12 st, Keep formants on, Mix 40 to 50 %.\n- Chipmunk hook: Pitch +12 st, Keep formants off.",
                "- Forgetting the latency: while Pitch is on it adds about 120 ms, which FL Studio and other DAWs compensate automatically. Live monitoring through the plugin is delayed.\n- Using it as pitch correction.",
                "Place Pitch before Doubler / Choir to double the shifted voice, or after them to shift the whole stack. Effects after Pitch (reverb, delay) follow the new pitch."));

            p.push_back (modulePage ("chop", "Vocal chop",
                "A tempo-synced gate and stutter: a 16-step pattern plays, mutes or repeats the vocal in time with the song.",
                "For build-ups, rhythmic breaks and stutter effects.",
                "- Pattern chooses the 16-step rhythm (the display shows the current step).\n- Step length sets the length of one step, synced to the host tempo (120 BPM when the host sends no tempo).\n- Depth sets how strongly the pattern is applied.\n- Smooth softens the edges of each step.",
                "- Lower Depth = the muted steps are only quieter, not silent.\n- Higher Smooth = softer, less clicky edges; too high blurs the rhythm.",
                "- Depth 100 %, Smooth 3 ms, a 1/16 step length.\n- Automate the power button in your DAW to use chops only in certain bars.",
                "- Leaving it on for the whole song by accident.\n- Very low Smooth on sustained notes (clicks).",
                "Put Chop before Reverb and Delay so the effects tail through the gaps, or after them to chop the effects too. Pulse is the smoother alternative for volume rhythms."));

            p.push_back (modulePage ("delay", "Delay",
                "Echoes of the vocal, with filters on the repeats and ducking that pushes the echoes down while the vocal is active.",
                "For depth, width and rhythm, especially at the ends of phrases.",
                "- Sync locks the time to the host tempo using Note; without Sync (or without a host tempo) Time is used.\n- Feedback sets the number of repeats, Mix their level.\n- Low cut and High cut filter the repeats.\n- Ping-pong alternates repeats left and right.\n- Into reverb sends the echoes into the reverb (when both run as sends).\n- Ducking: Amount, Threshold, Attack and Release; Duck source chooses the vocal or the sidechain input.\n- The Ins / Send switch on the chain tile decides series or parallel.",
                "- More Feedback = longer tail. Very high values build up.\n- Higher Mix = louder echoes.\n- More duck Amount = clearer words while singing, echoes bloom in the gaps.",
                "- Send, Sync on, 1/8 or 1/4 dotted, Feedback 25 to 35 %, Mix 15 to 25 %.\n- Low cut 300 Hz, High cut 6 kHz, duck Amount 6 to 10 dB.",
                "- Too much Mix without ducking: the vocal gets blurry.\n- Unfiltered repeats that fight with the lead in the high end.",
                "As a send it is not compressed or saturated by the chain. Space in Beginner mode drives Delay and Reverb together. Use Into reverb for a smooth, washed tail."));

            p.push_back (modulePage ("reverb", "Reverb",
                "Room and depth: a Room or Plate reverb with pre-delay, tone and low cut, plus ducking that clears space while the vocal is singing.",
                "For almost every lead in a club track - usually as a send with ducking.",
                "- Type: Room (natural, sparser) or Plate (dense, bright).\n- Mix sets the level, Decay the length, Size the room size.\n- Pre-delay leaves a gap before the reverb starts, so the words stay clear.\n- Tone damps the high frequencies in the tail; Low cut removes low end before the reverb; Width sets its stereo width.\n- Ducking as in the Delay.",
                "- Longer Decay = longer tail. Long tails at fast tempos make the mix muddy.\n- Longer Pre-delay = clearer separation between word and room.\n- Lower Tone = darker tail that sits behind the vocal.",
                "- Send, Plate, Decay 1.5 to 2.5 s, Pre-delay 20 to 40 ms, Mix 15 to 20 %.\n- Low cut 200 to 300 Hz, duck Amount 6 to 10 dB.",
                "- Too much low end in the reverb (keep the Low cut up).\n- Comparing reverb amounts in solo; decide in the mix.",
                "Put Reverb last or as a send. With Delay as a send, Into reverb feeds the echoes into the room. Ducking from the sidechain input (e.g. a kick) creates pumping reverbs."));

            p.push_back (modulePage ("pulse", "Pulse",
                "A rhythmic volume shaper: a drawable curve lowers and raises the volume in time with the song - for sidechain-style pumping, ducking and stutters.",
                "For pumping hard dance vocals, sidechain feel without a sidechain, and rhythmic gates.",
                "- Drag the points in the display to draw the volume curve (time from left to right, level from bottom to top).\n- Cycle sets the length of one curve cycle, synced to the host tempo; Manual BPM is used when the host sends no tempo.\n- Depth sets how far the curve pulls the volume down; Mix blends with the original.\n- Curve: Linear, Smooth or Step between the points; Shape bends every segment.\n- Attack, Release and Smooth round off the gain movement to avoid clicks.",
                "- Higher Depth = deeper dips.\n- Shape negative = fast start, positive = slow start (punchier pumps).\n- Longer Attack / Release / Smooth = softer movement.",
                "- Cycle 1/4, Depth 60 to 85 %, Curve Smooth.\n- Start the curve low at the beat and let it rise during the first third.",
                "- Very short Attack and Smooth with Step curves: clicks.\n- A cycle that does not match the kick pattern.",
                "Place Pulse late in the chain so earlier compression does not undo the pumping. Effects after it (reverb, delay) pump along; effects before it keep their tails."));

            p.push_back (modulePage ("smash", "Vocal Smash",
                "Parallel compression: a heavily squashed copy of the vocal is blended under the original. Density and loudness increase while the original keeps its punch.",
                "When the vocal needs to be louder and denser in a busy, loud track without losing its attack.",
                "- Amount sets how hard the parallel path is squashed (lower threshold, higher ratio).\n- Attack and Release set the speed of the squashed path.\n- Tone tilts its colour: negative warmer, positive brighter.\n- Blend sets how much of the squashed path is added.\n- Output trims the result.",
                "- Higher Amount = flatter squashed path.\n- Higher Blend = denser, louder vocal; too high loses the natural dynamics.\n- Short Release = denser and grainier, long = smoother.",
                "- Amount 50 %, Blend 30 to 40 %, Attack 2 ms, Release 80 ms, Tone +20 %.",
                "- Raising Blend until the vocal only seems better because it is louder - use Fair compare.\n- Very bright Tone that exaggerates S sounds.",
                "Usually after the Compressor (control first, then density) and before the De-esser. With Smash in use, the Compressor and Saturation can often do less."));

            p.push_back (modulePage ("choir", "Choir",
                "Turns one voice into a double or a whole choir: each virtual voice gets its own pitch, timing, slow drift and stereo position.",
                "For big hooks, festival choruses and backing layers.",
                "- Voices sets the number of virtual voices (2 to 8).\n- Detune, Timing and Humanize make the voices differ in pitch, timing and over time.\n- Width spreads them across the stereo field.\n- Density sets how strongly the voices are present.\n- Mix blends the choir with the original.\n- The display shows each voice's position and pitch offset, and a mono compatibility bar.",
                "- More Detune / Timing = looser and bigger, but less precise.\n- Higher Humanize = more natural movement.\n- Higher Mix = the choir takes over the original.",
                "- Voices 4 to 8, Detune 10 to 16 ct, Timing 15 to 30 ms, Humanize 50 to 70 %, Mix 30 to 45 %.",
                "- A huge choir on fast, rhythmic verses (it smears the rhythm).\n- Ignoring the mono bar: a negative value means parts cancel in mono.",
                "Use Choir or Doubler, rarely both at full level. Put Reverb after Choir so the voices share one room."));

            p.push_back (modulePage ("smooth", "Smooth",
                "Adaptive resonance control: finds frequencies that ring or stick out of their neighbours and lowers them only while they stick out. The display shows where and how much it reduces.",
                "For harsh, nasal or ringing vocals, cheap microphones and bright, compressed leads.",
                "- Amount sets how strongly resonances are reduced.\n- Sensitivity sets how far a frequency must stick out before it counts.\n- From and To limit the treated frequency range.\n- Mode: Soft (wide, gentle) or Hard (narrow, stronger, faster).\n- Attack and Release set the speed; Output trims the level.\n- Delta plays only what is being removed.",
                "- More Amount or Sensitivity = more reduction; too much makes the voice dull.\n- A wider From / To range treats more of the voice.",
                "- Amount 40 to 60 %, Sensitivity 50 %, From 800 Hz, To 12 kHz, Mode Soft.\n- Turn on Delta briefly: you should hear only harshness, not the voice itself.",
                "- Leaving Delta on.\n- Using Smooth to remove sibilance - the De-esser is made for that.",
                "Place it after compression and saturation, which bring resonances out. Static problems are often better fixed with an EQ cut first."));

            p.push_back (modulePage ("check", "Vocal Check",
                "Analyses the vocal at its position in the chain and explains what could be improved: balance between low, body, core, presence, S and air, level range, sibilance and clipping. It only listens - the audio passes through unchanged.",
                "When you are unsure what the vocal needs, and as a final check of your settings.",
                "- Switch it on and play the vocal; pauses are ignored. It needs about 4 seconds of voice before it shows results.\n- Mode: Continuous analyses the last 30 seconds or so; Capture analyses everything between Start and Stop.\n- Reset clears the analysis.\n- Click a finding to open the module it recommends.",
                "There are no sound controls - only the analysis mode. More material gives more reliable findings.",
                "- Put Vocal Check last and play a typical section of the song.\n- To judge the recording itself, drag it to the front of the chain.",
                "- Following every finding blindly. It describes the measurement; your ears and the track decide.\n- Analysing a section with a lot of silence or other instruments.",
                "It sees exactly what the modules before it did. Because it never changes audio, its position never changes the sound."));

            p.push_back (modulePage ("ride", "Level Ride",
                "Evens out loudness differences inside your vocal: quiet words are lifted and loud words are lowered, smoothly and without the pumping of a compressor. The display shows the detected level and the gain it applies.",
                "For vocals with big level jumps between words or phrases, before compression.",
                "- Target sets the level the rider aims for.\n- Amount sets how much of the distance to the target is corrected.\n- Max boost and Max cut limit the largest lift and reduction.\n- Response sets the speed: slow and smooth, or word by word.\n- Phrase averages over longer phrases to keep the natural flow.\n- Transients ignores short peaks such as consonants and plosives.\n- Ignore below treats quieter signals as pauses and never boosts them.",
                "- Higher Amount = more even; 100 % pulls everything towards the target.\n- Higher Max boost lifts quiet words more, but also breaths and room noise.\n- Higher Response = faster, more audible moves.",
                "- Amount 60 to 70 %, Max boost and Max cut 6 to 8 dB, Response 50 %.\n- Set Target near the average level of the vocal.\n- Start moderate and raise the intensity step by step.",
                "- Too much boost makes breaths and room noise louder - raise Ignore below.\n- Very fast Response: the vocal starts to wobble.",
                "Place it before the Compressor: the compressor then only has to catch short peaks. Pulse and Chop after it are not undone by the rider."));

            p.push_back ({ "output", "Output", gModules, juce::String() +
                "## " + moduleSectionHeadings()[0] + "\nThe last stage of the plugin: the stereo width of the whole output, mono safety in the low end, the loudness comparison and the clip guard.\n\n"
                "## " + moduleSectionHeadings()[1] + "\nAlways active. Open it by clicking Out at the end of the chain.\n\n"
                "## " + moduleSectionHeadings()[2] + "\n- Width changes only the side signal, so the centre and the mono sum stay intact.\n- Mono below keeps everything under the frequency in mono (20 Hz = off).\n- Level match: Off, Bypass match (fair bypass comparison) or Auto gain (processed sound as loud as the input). Measure over and Max correction set how it measures and how far it may correct.\n- Clip guard: soft clipper from -1 dBFS that keeps the output below 0 dBFS.\n- The goniometer and correlation show the stereo image.\n\n"
                "## " + moduleSectionHeadings()[3] + "\n- Width above 100 % widens, below narrows, 0 % is mono.\n- Higher Mono below = more of the low end in mono.\n\n"
                "## " + moduleSectionHeadings()[4] + "\n- Width 100 to 120 %, Mono below 120 Hz for club playback.\n- Level match: Bypass match while you work.\n\n"
                "## " + moduleSectionHeadings()[5] + "\n- Extreme Width that pushes correlation below 0.\n- Forgetting that Auto gain changes the output level continuously.\n\n"
                "## " + moduleSectionHeadings()[6] + "\nWidth in Beginner mode drives the output width together with the Doubler. Output gain on the right rail sets the final level after this stage." });

            return p;
        }
    }

    const juce::StringArray& moduleSectionHeadings()
    {
        static const juce::StringArray h { "What it does", "When to use it", "How to use it", "Higher and lower values",
                                           "Good starting points", "Common mistakes", "Working with other modules" };
        return h;
    }

    const std::vector<Page>& pages()
    {
        static const std::vector<Page> p = build();
        return p;
    }

    const Page* find (const juce::String& key)
    {
        for (auto& page : pages())
            if (page.key == key)
                return &page;
        return nullptr;
    }

    juce::String keyForModule (int id)
    {
        if (id < 0 || id >= dsp::numModules)
            return "output";
        return dsp::moduleKey ((dsp::ModuleId) id);
    }
}

namespace velvox::ui::help
{
    juce::String shortDescription (int id)
    {
        using dsp::ModuleId;
        if (id < 0 || id >= dsp::numModules)
            return "Stereo image, mono safety and fair loudness comparison";

        switch ((ModuleId) id)
        {
            case ModuleId::eq:      return "Remove rumble, shape body, presence and air";
            case ModuleId::comp:    return "Evens out the level so every word sits in the mix";
            case ModuleId::deess:   return "Tames sharp S and T sounds without dulling the voice";
            case ModuleId::sat:     return "Adds harmonics for warmth, grit and density";
            case ModuleId::doubler: return "Two extra takes left and right for a wide, thick vocal";
            case ModuleId::pitch:   return "Creative transposition (not pitch correction)";
            case ModuleId::chop:    return "Tempo-synced gate and stutter patterns";
            case ModuleId::delay:   return "Echoes beside the voice; ducking keeps the words clear";
            case ModuleId::reverb:  return "Room and depth; ducking clears space while you sing";
            case ModuleId::pulse:   return "Rhythmic volume shaping: pumping, ducking and stutter, locked to the beat";
            case ModuleId::smash:   return "Parallel compression: density and loudness, the original keeps its punch";
            case ModuleId::choir:   return "From one voice to a double or a whole choir - each voice with its own pitch and timing";
            case ModuleId::smooth:  return "Finds ringing, harsh frequencies and tames them only while they stick out";
            case ModuleId::check:   return "Analyses the vocal at this point of the chain and explains what could be improved";
            case ModuleId::ride:    return "Lifts quiet words, lowers loud ones - natural, without pumping";
        }
        return {};
    }
}
