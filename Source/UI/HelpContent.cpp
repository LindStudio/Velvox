#include "HelpContent.h"

namespace velvox::ui::help
{
    namespace
    {
        const char* const gStart = "Getting started";
        const char* const gWork = "Working with Headliner";
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
            p.push_back ({ "start", "Welcome to Headliner", gStart, R"(Headliner is an automatic vocal mixer for EDM, hypertechno, hard dance, party and pop vocals. It listens to a section of your vocal, measures it, and sets up its modules - cleaning, dynamics, S sounds, tone, width and space - for a finished-sounding vocal. Every module stays fully editable afterwards.

## Your first minute
- Load Headliner on the mixer track of your vocal. A new instance asks: "Let's mix your vocal."
- Click "Vocal analysieren" and start playback of the vocal in your DAW. Headliner captures a limited section (pauses are skipped) and stops by itself.
- The analysis and the mix run in the background; the playback is never interrupted.
- Compare with Vorher / Nachher next to the button, and fine-tune with the six faders.

## SIMPEL and ADVANCED
- SIMPEL: the live spectrum of your vocal (before = dashed line, after = filled curve), "Vocal analysieren", and six macro faders: Presence, Space, Width, Punch, Warmth, Vocal Level. Small IN / OUT meters at the bottom.
- ADVANCED: the module chain on top and the panel of the selected module below - a large live picture, the main controls, and everything else behind "Mehr".

## The tabs AUTO MIX and VOCAL CHECK
- AUTO MIX: your mix, in SIMPEL or ADVANCED.
- VOCAL CHECK: a diagnosis of the recording with suggestions.
Switching mode or tab never changes or resets a value.

## Help is always here
The ? button at the top right opens this guide. Inside a module, the ? button jumps straight to the page for that module. You can restart the tutorial from the bottom of the navigation.)" });

            p.push_back ({ "chain", "The signal chain", gStart, R"(In ADVANCED the row under the header is the signal chain. Audio runs through the modules from left to right and leaves at OUTPUT (click it for the output stage).

## Reading a tile
- Each tile shows the module's name, its icon and its on/off switch. A switched-off tile is dimmed and the module is bypassed.
- A thin line along the bottom moves while the module is working (for example while a compressor reduces gain).
- Delay and Reverb can run as a parallel send: they then sit behind the dashed line and carry a SEND badge; their Mix knob reads SEND.

## Why order matters
Every module hears what the modules before it did. Typical consequences:
- Level Ride before the Compressor gives the compressor an even input, so it can work gently.
- Saturation after the De-esser does not exaggerate S sounds; before it, the de-esser also tames the harmonics saturation created.
- Reverb and Delay at the end keep the effects from being compressed and saturated.
- Vocal Check analyses the signal at its own position. Put it last to judge the result, first to judge the recording.
The default order follows these rules and is a safe place to start.)" });

            p.push_back ({ "modules", "Organising modules", gWork, R"(## Open a module
Click a tile: its panel appears below the chain.

## Switch on and off
Click the switch of a tile, or the one in the panel header. A switched-off module is bypassed: it is removed from the active processing and costs no CPU.

## Change the order
Drag a tile: a glowing marker shows where it will land, and the audio order changes at the same moment (with a short, click-free crossfade). Dragging starts only after the mouse has moved a few pixels, so a click never moves a module.

## Right-click a tile
Bypass, Duplizieren (a copy with the same settings), Entfernen, Nach links / Nach rechts, Zurücksetzen (every value back to its default), Als Send / Als Insert (Delay and Reverb), Rückgängig / Wiederholen.

## Add modules
The + tile at the end of the row, or a + on the gap between two tiles, opens the module browser: cards grouped by Clean-up, Dynamics, Colour, Width / Voices, Space, Rhythm and Analysis, with a search field. Enter adds the highlighted card, Esc closes. The same module can be used several times; each instance has its own settings.

## Layouts
Right-click the empty part of the row for the chain menu: sort into the usual order, save this chain as your default (for all projects), load your default chain.)" });

            p.push_back ({ "controls", "Controls and values", gWork, R"(Every knob, fader and segment group works the same way:
- Drag to change the value (up or right raises it).
- Hold Shift or Ctrl while dragging for fine adjustment (1/10 of the speed).
- Double-click, or Alt-click, to return to the default.
- The mouse wheel steps the value while the pointer is over the control (lists keep the wheel for scrolling).
- Click the value to type it. Units are understood: "2k" = 2000 Hz, "-3 dB", "1.5 s", "50 %", note values such as "1/8".
- Right-click for a menu: Standardwert, Wert eingeben, Wert kopieren, Wert einfügen - and inside a module panel also Modul zurücksetzen, Modul-Preset speichern.
- Rest the mouse on a control for a moment: its name, what it does and its current value appear.

## Undo
Every change is one step of one history: Ctrl+Z undoes, Ctrl+Y or Ctrl+Shift+Z redoes. Moving a module, loading a preset and the automatic mix are one step each.

## Automation
Every knob, switch and menu is a real plugin parameter: your DAW can automate it and presets store it. Headliner reports changes to the host as proper gestures, so recorded automation is clean.)" });

            p.push_back ({ "modes", "Modes, tabs and the macros", gWork, R"(## SIMPEL
- The spectrum shows your vocal before (dashed) and after (filled) Headliner.
- "Vocal analysieren" starts the automatic mix. Afterwards a small Vorher / Nachher switch compares with the state before; Details shows what was set up and why, Density and the fair compare.
- The six faders: Presence (highs and de-essing), Space (ducked reverb and delay), Width (doubler and stereo width), Punch (faster, firmer compression and drive), Warmth (body, softer highs, saturation), Vocal Level (the final output level). They work on top of the automatic mix and move the real parameters.

## ADVANCED
The module chain with the panel of the selected module. When a macro moves a parameter, its knob shows a small marker at the value that is actually processed.

## VOCAL CHECK
A diagnosis of the recording at the position of the Vocal Check module, with suggestions.

## Switching
All views show the same settings. Switching never changes the sound or resets anything.)" });

            p.push_back ({ "automix", "Automatic mix", gWork, R"(## What it does
Headliner records a limited section of the vocal while your DAW plays it (about 14 seconds of singing, at most 24 seconds including short gaps), analyses it and sets up the modules. Everything runs inside the plugin: no internet, no cloud, no AI service. A plugin only hears what the DAW plays through it - Headliner never reads the audio file itself, so only the captured section is analysed.

## What is measured
- Loudness (ITU-R BS.1770), peaks, clipping, the level difference between loud and quiet words, crest factor and noise floor (when there are pauses).
- Pitch range of the voice, the long-term spectrum (rumble, body, low mids, presence, S range, air), S sounds against vowels, narrow resonances (partials of the sung notes are excluded), room or effect tails, and stereo width.
- With a beat on the sidechain: its level and how much it overlaps the voice's presence range. This is only a hint, never proof of masking - the beat is never heard through Headliner.

## What is set up
The decisions follow the measurements: the low cut sits well below the lowest sung notes, highs are only raised on dark recordings, the de-esser threshold sits between vowel and S level, resonance control only runs when something sticks out, reverb and delay are reduced or left off on roomy recordings. Creative effects (Pitch, Stutter, Pulse, Choir) stay off.

## Checked before it is applied
Headliner renders the captured section through the complete chain, measures the result and corrects it where the measurement is reliable: compression that is too strong or too weak, S sounds that the chain made louder, lost body or too much brightness, mono compatibility. Finally the output is matched to the loudness of the original (so the comparison is fair) with 1 dB of headroom.

## Compare and undo
- Vorher / Nachher (SIMPEL, next to "Vocal analysieren") switches the real parameters between the state before the analysis and the mix (including your changes to it).
- Bypass in the header plays the unprocessed vocal; with the fair compare set to Bypass match at the same loudness.
- "Vorherigen Zustand wiederherstellen" (Details) discards the mix; Ctrl+Z undoes the mix as one step.
- "Vocal analysieren" is always available; the current state is kept as the new "Vorher".

## Limits
A short or quiet capture gives conservative settings (and says so). The analysis judges the recording, not your taste - treat the result as a well-prepared starting point and listen. It is not a guarantee of a perfect mix for every recording.)" });

            p.push_back ({ "presets", "Presets and saving", gWork, R"(## The preset list
The header shows ‹ name ›. The arrows step through all presets; click the name for the list:
- Categories on the left: Alle, the factory categories (Clean, Punchy, Aggressive, Wide, Creative, ...), Eigene (your presets) and Favoriten.
- Type in the search field to filter by name, category or description.
- Click the star of a preset to make it a favourite (kept for all projects).
- Rest the mouse on a preset to read its description; double-click or Enter loads it (one undo step).

## Your own presets
- Speichern overwrites the loaded preset of your own; with a factory preset it asks for a name.
- Speichern als, Umbenennen and Löschen work on your own presets. Factory presets are never changed.
- User presets are files in your Documents folder (Lind Audio/Headliner/Presets), so you can back them up or share them.
- A * after the name in the header means the loaded preset was changed.

## Module presets
The Presets button inside a module panel loads starting points for that module only; the rest of the chain stays as it is. Right-click in the panel and choose Modul-Preset speichern to keep your own (they appear under Eigene in that menu).

## What presets do not touch
Input gain, Bypass and the fair compare / Level match settings belong to your recording and monitoring, so presets never change them.

## Projects
Your DAW project stores the complete state of every Headliner instance: all parameters, the module order, the mode and tab, the window size, the automatic-mix report and the state before it.)" });

            p.push_back ({ "compare", "Compare: Bypass and Vorher / Nachher", gWork, R"(## Bypass
Bypass (top right) plays the unprocessed vocal, delay-compensated so it stays in time. While bypass is on, Headliner shows an amber frame and a label so you never forget it. It is also the host's bypass parameter.

## Vorher / Nachher
After an analysis, SIMPEL shows Vorher / Nachher next to "Vocal analysieren": it switches between the settings before the analysis and the mix. Switching is listening, not editing, so it is not an undo step.

## Fair comparison
A louder sound almost always seems better. Set the fair compare (Details) or Level Match (OUTPUT) to:
- Bypass match: Bypass plays as loud as the processed vocal, so you compare tone, not loudness.
- Auto gain: the processed vocal is continuously kept as loud as the input.
- Off: no level adjustment.
Level matching never happens silently: it only works when you choose it.)" });

            p.push_back ({ "meters", "Meters and displays", gWork, R"(## IN and OUT
- SIMPEL shows them at the bottom, ADVANCED at the right end of the chain row.
- The bars show the peak level, the number is the loudness in LUFS (short term).
- The red lamp on OUT lights when the signal went above 0 dBFS. Click the meters to reset it.

## In the OUTPUT panel
- Clip guard (Mehr): a soft clipper from -1 dBFS that keeps the output below 0 dBFS.
- Mono (Mehr): listen in mono to check compatibility (not saved with the project).
- Correlation: +1 is mono-safe, around 0 is very wide, below 0 parts of the sound cancel in mono.

## Module displays
Each module shows real data from the audio or from its own settings: the EQ curve over the live spectrum, the compressor's gain reduction, the de-esser band, the doubler and choir voices, the pulse curve with the gain that is applied, the level ride history and more. Nothing on screen is simulated.)" });

            p.push_back ({ "problems", "Fixing common vocal problems", gWork, R"(- Rumble, plosives, muddy low end: EQ Low cut around 80 to 120 Hz. For plosives that trigger the compressor, raise the Compressor's SC filter (Mehr).
- Boxy or honky: EQ Mid, cut 2 to 4 dB between 300 and 800 Hz with a moderate Q.
- Harsh, ringing resonances: Smooth with moderate Depth; narrow, sharp spots: EQ Mid with a high Q.
- Sharp S and T sounds: De-esser, find the band with Listen, then set Threshold so only the S sounds trigger.
- Uneven words, some disappear: Level Ride before the Compressor.
- Not loud or dense enough: Vocal Smash blended under the vocal, then the Compressor for control.
- Too dry or too far back: Reverb and Delay with ducking (Mehr), so the words stay clear.
- Too narrow: Doubler or Choir, then check Mono in the OUTPUT panel.
- Clipping output: lower Vocal Level and keep Clip guard on.
- Not sure where to start: open VOCAL CHECK and play the vocal for a while. A finding opens the module it recommends.)" });

            //==================================================================
            p.push_back (modulePage ("eq", "Equalizer",
                "Shapes the tone of the voice with up to 16 bands of your own: low cut against rumble, bells for boxiness or presence, shelves for body and air, a high cut for darker or lo-fi sounds. The curve is drawn over the live spectrum (before faint, after filled).",
                "Almost always, as the first cleaning step. Use it to remove what you do not want and to give the vocal its basic colour.",
                "- Double-click on empty space adds a Bell at that frequency and gain.\n- Drag a point: frequency and gain (Shift or Ctrl: fine). Mouse wheel over a point: Q (on a cut: the slope).\n- Double-click a point switches it on or off, Alt+click resets its gain and Q.\n- Right-click a point: type (Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass, Tilt), slope of a cut (6 to 48 dB/oct), on/off, delete. The Delete key removes the selected band.\n- Under the graph: the selected band's On, Type, Freq, Gain and Q (Slope for cuts), to type exact values.\n- Bands with a small tag (AIR, WARM, LC, AUTO ...) belong to the macros or the automatic mix. You can edit or delete them; a macro brings its band back when you move it.\n- Mehr: Mix blends the equalized and the original signal.",
                "- Gain: positive values boost, negative values cut (the display shows +/-18 dB, the value field takes +/-30 dB).\n- Q: higher = narrower bell, lower = broad and musical.\n- Low cut frequency: higher removes more low end; too high makes the voice thin.\n- Slope: 24 dB/oct cuts more steeply than 12 dB/oct.",
                "- Low cut 80 to 120 Hz, 24 dB/oct.\n- A Bell -2 to -4 dB around 400 Hz if the voice sounds boxy.\n- A High Shelf +2 to +4 dB at 10 kHz for air.",
                "- Boosting many bands instead of cutting the problem.\n- Very narrow, large boosts that start to ring.\n- Setting the low cut so high that the voice loses its body.\n- Judging only by the curve - always listen.",
                "Put the EQ before the Compressor so the compressor does not react to rumble. Large high boosts can make S sounds sharper; follow with the De-esser. Smooth catches resonances that move, which a static EQ cannot. Projects from Headliner 0.5 keep their old filter curves; Mehr offers 'Alte EQ-Kurve - umstellen' to switch to the new analog-matched curves."));

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
                "Place it after EQ boosts and compression, which both make S sounds louder. Presence (macro fader in SIMPEL) raises the de-essing automatically together with the highs. Smooth handles ringing resonances, the de-esser handles sibilance."));

            p.push_back (modulePage ("sat", "Saturation",
                "Adds harmonics: warmth, grit, density and a stronger presence in a loud mix. The drive is level compensated, so it changes character rather than volume.",
                "When the vocal sounds too clean, thin or polite for an energetic track.",
                "- Character: Warm (smooth, slightly asymmetric), Tape (gentle, symmetric), Tube (rich even harmonics), Hard (firm clipping).\n- Drive sets the amount of saturation.\n- Output trims the level after the stage.\n- Mix blends the saturated and the clean signal.",
                "- More Drive = more harmonics and more audible distortion.\n- Lower Mix = subtler, keeps the clean transients.\n- Hard character distorts most at the same Drive.",
                "- Warm or Tape, Drive 4 to 8 dB, Mix 100 %.\n- For aggressive leads: Hard with Mix 30 to 50 %.",
                "- Too much drive on S sounds - put the De-esser before it or lower Drive.\n- Judging distortion only on solo; in the mix more is often fine, but check.",
                "Saturation increases density similar to compression; with Vocal Smash or heavy compression use less. Warmth, Punch and Density (macro fader in SIMPEL) add saturation automatically."));

            p.push_back (modulePage ("doubler", "Doubler",
                "Creates two extra takes left and right with small timing and pitch differences, for a wide and thick vocal while the original stays in the centre.",
                "For choruses, hooks and adlibs that should feel bigger and wider.",
                "- Mix sets the level of the doubled voices; the original stays at full level.\n- Width places the voices left and right.\n- Timing sets the delay of the doubles (longer feels looser).\n- Detune sets their pitch variation.\n- Mono safe keeps the doubles out of the mono sum.\n- Tone darkens the doubles so they sit behind the lead.",
                "- Higher Mix = more obvious doubling.\n- More Detune and Timing = looser, more chorus-like; too much sounds out of tune or echoey.\n- Mono safe 100 % = mono playback sounds exactly like the original.",
                "- Mix 40 to 50 %, Width 80 %, Timing 12 to 18 ms, Detune 6 to 10 ct, Mono safe 50 to 100 %.",
                "- Too much Detune on a tuned lead.\n- Forgetting the club: check with Mono in the OUTPUT panel.",
                "Choir is the bigger brother for whole groups of voices; you rarely need both at full level. Width (macro fader in SIMPEL) drives the doubler. Put reverb after the doubler so both doubles get the same room."));

            p.push_back (modulePage ("pitch", "Pitch",
                "Creative transposition: shifts the vocal by semitones and cents, with optional formant control. It is not a pitch correction tool.",
                "For octave layers, chipmunk hooks, dark monster voices and effects.",
                "- The big Semitones control transposes in semitones (it snaps; the ring marks the octaves), Fine adds cents.\n- Between the two big controls you see the note that is sung (IN) and the note that comes out (OUT).\n- Formanten erhalten keeps the natural character of the voice; off moves the character with the pitch.\n- Formant moves the character independently of the pitch.\n- Mix blends the shifted voice with the original (for layers).",
                "- Large shifts sound more artificial, especially downwards.\n- Formant up = smaller, brighter voice; down = bigger, darker voice.\n- Mix 50 % = octave layer; 100 % = only the shifted voice.",
                "- Octave layer: Semitones -12, Formanten erhalten on, Mix 40 to 50 %.\n- Chipmunk hook: Semitones +12, Formanten erhalten off.",
                "- Forgetting the latency: while Pitch is on it adds about 15 ms, which FL Studio and other DAWs compensate automatically. Live monitoring through the plugin is delayed.\n- Using it as pitch correction.",
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
                "- Sync locks the time to the host tempo using Note; without Sync (or without a host tempo) Time is used.\n- Feedback sets the number of repeats, Mix their level.\n- Low cut and High cut filter the repeats.\n- Ping-pong alternates repeats left and right.\n- Into reverb sends the echoes into the reverb (when both run as sends).\n- Ducking: Amount, Threshold, Attack and Release; Duck source chooses the vocal or the sidechain input.\n- Right-click the tile: Als Send / Als Insert decides parallel or series. As a send, Mix reads SEND (the send level).",
                "- More Feedback = longer tail. Very high values build up.\n- Higher Mix = louder echoes.\n- More duck Amount = clearer words while singing, echoes bloom in the gaps.",
                "- Send, Sync on, 1/8 or 1/4 dotted, Feedback 25 to 35 %, Mix 15 to 25 %.\n- Low cut 300 Hz, High cut 6 kHz, duck Amount 6 to 10 dB.",
                "- Too much Mix without ducking: the vocal gets blurry.\n- Unfiltered repeats that fight with the lead in the high end.",
                "As a send it is not compressed or saturated by the chain. Space (macro fader in SIMPEL) drives Delay and Reverb together. Use Into reverb for a smooth, washed tail."));

            p.push_back (modulePage ("reverb", "Reverb",
                "Room and depth: eight spaces from a small Room to Atmosphere, with the vocal as a dot in the room - drag it back for more space. Ducking clears space while the vocal is singing.",
                "For almost every lead in a club track - usually as a send with ducking.",
                "- The picture: the room of the chosen type and size, the vocal as a dot. Drag the dot back (or drag anywhere in the room, up = back): Mix, Pre-delay and Size move together - in front a long gap and little reverb, far back a lot of reverb and a big room. Shift = fine, double-click = default, wheel = small steps, right-click = Vorne / Mitte / Hinten. The faint curve on the back wall shows how long the tail rings from low (left) to high (right).\n- Room: Room, Plate, Hall, Large Hall, Ambience, Atmosphere, Cinematic, Ethereal.\n- Size, Decay (the length of the whole tail) and Mix (SEND as a send) sit under the picture.\n- Mehr: Pre-delay, Low cut, High cut, Damping, Bass decay, Width, Early, Diffusion, Movement and ducking (as in the Delay).",
                "- Longer Decay = longer tail. Long tails at fast tempos make the mix muddy.\n- Longer Pre-delay = clearer separation between word and room.\n- Lower Damping = darker tail that sits behind the vocal.",
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
                "Adaptive resonance control: finds frequencies that ring or stick out of their neighbours and lowers them only while they stick out. The display shows the live spectrum of the vocal; what is removed right now is drawn in pink.",
                "For harsh, nasal or ringing vocals, cheap microphones and bright, compressed leads.",
                "- Depth sets how strongly resonances are reduced.\n- Sharpness sets how far a frequency must stick out before it counts.\n- Speed sets how fast it reacts and lets go (Attack and Release together; both are in Mehr separately).\n- The curve with six points weights the treatment per frequency: drag a point up for more reduction there, down for less (x = frequency). Double-click a point to reset it; right-click for 'Alle flach'.\n- The shaded areas left and right are not treated: drag their edges (Low / High).\n- Mehr: Mode Soft (wide, gentle) or Hard (narrow, stronger), Attack, Release, Output, Low, High.\n- Delta plays only what is being removed.",
                "- More Depth or Sharpness = more reduction; too much makes the voice dull.\n- A wider Low / High range treats more of the voice.\n- A point at -12 dB nearly stops the treatment around its frequency.",
                "- Depth 40 to 60 %, Sharpness 50 %, Low 800 Hz, High 12 kHz, Mode Soft.\n- Turn on Delta briefly: you should hear only harshness, not the voice itself.",
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
                "## " + moduleSectionHeadings()[6] + "\nWidth (macro fader in SIMPEL) drives the output width together with the Doubler. Vocal Level (the macro fader, also a main control of this panel) sets the final level after this stage; Input Gain sits here too." });

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
