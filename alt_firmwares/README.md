# Plaits

[discussion](https://llllllll.co/t/mutable-instruments-alt-firmwares/63936)

[download](https://github.com/lylepmills/eurorack/raw/master/alt_firmwares/plaits_20231210.wav)

*update 12/11/23: there has been a minor revision to remove the effect of the envelope on the aux output when using it as a suboscillator, to simplify its use as a sync output. if aux crossfade is enabled while the aux output is a suboscillator, then it will once again be enveloped*

*update 9/1/23: this is a new version of the alt firmware built on top of version 1.2 of the official firmware, with some new options and a new interaction model for frequency locking*

The Plaits alt firmware extends the base firmware with several additional features accessed through a menu. Originally, this alt firmware was created primarily around the idea of allowing frequency locking. However in revision 1.2 of the official firmware, Émelie incorporated the frequency locking idea elegantly into her official firmware with the addition of the octave-switching mode to the frequency range selector. As such it was no longer necessary to use this alt firmware to get frequency locking behavior.

However my alt firmware had several other features that I felt were still worthwhile, so in August 2023 I revised it to use Émelie's approach to frequency locking as well as incorporating the rest of the updates from revision 1.2, while retaining some of the other features I had added, and adding a couple of new ones as well.

## Features of this alt firmware

- **Aux crossfading.** - It's possible with this firmware to crossfade between the regular and aux models on the aux output, essentially giving you a fourth timbral dimension for each model. The crossfade can be controlled either manually by repurposing the frequency knob, or under CV control (or both).
- **CV control of LPG parameters.** - The MODEL and LEVEL CV inputs can be repurposed (either both or individually) to put the colour and decay of the internal LPG under CV control.
- **Suboscillator.** The aux model can be replaced by a square or sine wave suboscillator on the aux output. The square wave can be used to sync another oscillator with a sync input (like Tides), an oscilloscope, etc. This is fully compatible with aux crossfading, meaning you can blend the main model with a square or sine wave. There are also options for the suboscillator frequency to be either the same frequency as the main model, -1 octave, or -2 octaves.
- **Alternate chord tables.** For the chord-based modes (the 7th green mode, and the 7th and 8th yellow modes), you can switch between the original table of 11 chords, or alternative chord tables by Jon Butler (17 chords) or Joe McMullen (18 chords). See below for more details on each chord table.
- **Hold parameters on trigger.** - It is possible to internally sample and hold the CV inputs for Timbre, Morph, Harmonics, Level and V/Oct whenever a trigger is received.
- **Easier manual control over decay.** - The frequency knob can be repurposed to more easily control the decay of the internal LPG, without having to hold down a button.
- **More convenient model switching.** - In revision 1.2 of the official firmware, an alternate mode for switching models was introduced. This mode was the only way of accessing the third bank of models. My assumption is that anyone using this alt firmware wants to be able to freely access all three banks of synthesis models, so in this alternate firmware, that mode is now the default. Additionally there is a new navigation mode (an alternative to the alternative), whereby the left button switches between models within a bank and the right button switches banks.

## How to access calibration
Calibration has been removed from the alt firmware to save space. If you need to recalibrate, simply reinstall the official firmware first. Your calibration settings will be preserved if you then switch back to this alt firmware.

## How to access the menu / change options
To access the menu of options for the various alt functionalities, simply short-press both buttons at once. To exit the menu, again press both buttons at once.

The menu is represented by lights for each option. You can navigate between the menu items by pressing the left button, and you can switch the option for that item by pressing the right button. The current settings are represented by the color of the LED lights. In order, they represent the following

### First light - Frequency knob alt functionality (when frequency is locked i.e. in octave-switching mode)
Green means octaves (as in the stock firmware), red means controlling decay, yellow means controlling aux crossfade

### Second light - MODEL input alt functionality
Green means model (as in the stock firmware), red means LPG colour (VCFA->VCA), yellow means aux crossfade

### Third light - LEVEL input alt functionality
Green means level (as in the stock firmware), red means decay of the internal envelope (if and only if TRIG is patched)

### Fourth light - Aux suboscillator wave option
Green means the regular aux model, red means a square wave subosc, yellow means a sine wave subosc

### Fifth light - Aux suboscillator octave option
Green means the +0 octaves (same frequency as the main model), red means -1 octave, yellow means -2 octaves. Note this will only have an effect if the suboscillator wave option is set to something other than the regular aux model.

### Sixth light - Chord table option (chord mode only)
Green means the original chord table, red means the alternate chord table by Jon Butler, yellow means the alternate chord table by Joe McMullen

#### Original chords
- Octave
- Fifth
- Sus4
- Minor
- Minor 7th
- Minor 9th
- Minor 11th
- 69
- Major 9th
- Major 7th
- Major

#### Alternate chords 1 (by Jon Butler)
Organized (from full-CCW to full-CW) into neutral, minor, major, and colour chords
- Octave
- Fifth
- Minor
- Minor 7th
- Minor 9th
- Minor 11th
- Major
- Major 7th
- Major 9th
- Sus4
- 69
- 6th
- 10th (Spread maj7)
- Dominant 7th
- Dominant 7th (b9)
- Half Diminished
- Fully Diminished

#### Alternate chords 2 (by Joe McMullen)
With this table, instead of Frequency defining the root and Harmonics defining chord type, Frequency defines the key and Harmonics defines the scale position the chord is built from, meaning the root you hear on the aux output shifts around. Note that chord voicings are also more spread out by default with this table. See [this writeup from Joe](https://github.com/lylepmills/eurorack/blob/master/alt_firmwares/README.md#disastrous-peace) for more info. If that doesn't make sense, don't worry, just use your ears!
- iv 6/9
- iio 7sus4
- VII 6
- v m11
- III add4
- i addb13
- VI add#11
- iv m6
- iio
- viio
- V 7
- iii add b9
- I maj7
- vi m9
- IV maj9
- ii m7
- I maj7sus4/vii
- V 7sus4

### Seventh light - Hold params on trigger
Green means don't hold params upon receiving a trigger (as in the stock firmware), red means internally sample and hold the CV inputs for Timbre, Morph, Harmonics, Level and V/Oct whenever a trigger is received.

### Eighth light - Model navigation mode
Green means the "alternative" navigation mode from the stock firmware, whereby the left button goes backward and the right button goes forward through the set of all models. Red means the "alternative alternative" navigation mode, where the left button cycles through models in a given bank, and the right button switches banks. Note the standard navigation mode from the official firmware is not available, since it doesn't allow access to the third bank and is therefore not very useful in my view.

## Technical notes
The original Plaits firmware makes the most of its processor, and runs quite close to that processor's limits in terms of both the amount of code it can store, and the CPU load of that code (at least, in certain modes). Unlike earlier versions of this firmware, various efficiency improvements have been found that made the compromises I had to use in those earlier versions unnecessary. However it's worth noting that it's always possible there is some combination of settings I haven't discovered yet that will lead to CPU overload, which can lead to harsh aliasing sounds coming from the module. If you encounter such a situation, please take note of the settings you are using, the model you are using, and where you have all the knobs positioned, and send me a report (ideally with video), and I will try to make adjustments to avoid those kinds of problems going forward.

# Rings

[discussion](https://llllllll.co/t/mutable-instruments-alt-firmwares/63936)

[download](https://github.com/lylepmills/eurorack/raw/master/alt_firmwares/rings_mini_elements.wav)

*update 2/27/22: the first release of this firmware purported to have a way of accessing calibration on powerup, however it didn't work correctly. if you are using a version of this firmware downloaded prior to 2/27/22, you won't be able to access calibration. if you download the firmware after 2/27/22, the procedure to reach calibration described below will work.*

The Rings alt firmware extends the base firmware with several additional features, all configurable with a menu. The main new addition is a "Mini-Elements" mode which borrows some of the functionality of Elements to give Rings a wider palette of options for internal excitation.

## Features of this alt firmware

- **Frequency locking.** It is now possible to lock the frequency of Rings, so you can avoid accidentally detuning it once you've tuned it where you want it. When locked, the main frequency knob will no longer affect the frequency of the resonator (the V/Oct and FM inputs still work as normal, as does the FM attenuverter).
- **Frequency knob alt functionality.** When frequency is locked, the frequency knob can control something else. In Mini-Elements modes, it controls the Timbre of the internal exciter. In all other modes, it is an octave switch.
- **Mini-Elements modes.** There are two new modes that give Rings an internal exciter, borrowed directly from Elements. See below for more details.
- **Alternate chord tables.** For the easter egg mode and the quantized sympathetic string modes, both of which are based on chords, you can switch between three different chord tables. See below for more details.
- **Hold on Strum.** There is now an option to make Rings sample-and-hold all resonator parameters (except base frequency), and most exciter parameters, in Mini-Elements modes, on every strum (i.e. a trigger to STRUM if STRUM is patched, or anything else Rings considers a "strum" when STRUM isn't patched such as a note change to V/OCT). In this way, when using Rings polyphonically, each voice can ring out with different values for Structure, Brightness, Damping, and Position.
- **Basic waveform outputs.** It is possible to repurpose the EVEN output to output a classic waveform (square, sawtooth, or pulse train) at the same frequency as the base frequency of the resonator. This can be used for tuning, syncing another oscillator module, feedback patching, or any other creative purposes you can think of.

## How to toggle frequency locking
To lock or unlock frequency, briefly hold both buttons at once until all the lights start flashing either green or red. Green means that frequency is unlocking, and red means it is locking. The actual locking or unlocking will only take place when you release (this is to avoid unintentional locking/unlocking when trying to reach the menu).

## How to access calibration
To access calibration, long press both buttons (~3s) *starting with the one on the right* (starting with the one on the left will take you to the options menu). Otherwise the calibration process is the same as the base firmware.

## How to change the other options
To access the options menu, long press (~3s) both buttons *starting with the one on the left* (starting with the one on the right will take you to calibration). Once you've held the buttons long enough, the LEDs will flash with a little animation where they switch between red, green, and yellow for a while, then you are in the menu. To leave the menu, press both buttons at once and once again there will be a little animation, only this time the LEDs will only alternate between red and green. While you are still in the menu, every 10 seconds the left (polyphony) LED will do a little animation where it flashes through green/yellow/red - this is just to remind you that you are in the menu, so you are less likely to get confused about what your next button press will do.

There are currently four different “pages” in the option menu. Since Rings only has two LEDs, the “page” is denoted by the color of the left (polyphony) LED and the value of that option is denoted by the color of the right (resonator) LED. To switch pages, press the polyphony button. To switch options for the given page, press the resonator button.

Here are the options by page.

### Page 1: Solid green - Overall Mode (5 options)
This option controls the top-level operating mode of the module, other options and interactions depend on it
* Solid green / **Classic Rings** (in stereo)
* Blinking green / **Rings + Waveform**. In this mode you Get a mono version of classic Rings on ODD with a synced waveform output on EVEN (for tuning, syncing, or whatever creative purposes you come up with)
* Solid red / **Mini-Elements** (in stereo)
* Blinking red / **Mini-Elements + Exciter**. In this mode you get the resonator output on ODD and a raw exciter signal on EVEN. The idea of this mode is to take the raw exciter signal and apply processing to it (such as enveloping it) before sending it back to IN.
* Solid yellow / **Easter Egg** mode from original Rings, AKA [Disastrous Peace](https://synthmodes.com/modules/rings/#peace) mode. Note in this firmware the old handshake for reaching the Easter Egg mode has been removed, so switching to this mode using the menu is the only way to access it.

### Page 2: Solid red - Waveform/Exciter (3 options)
In **Rings + Waveform** mode, this option selects a waveform, whereas in **Mini-Elements** modes it selects the exciter type
* Solid green: Square (in rings+waveform mode) / Bow (in Mini-Elements modes)
* Solid red: Impulse Train (in rings+waveform mode) / Blow (in Mini-Elements modes)
* Solid yellow: Sawtooth (in rings+waveform mode) / Strike (in Mini-Elements modes)

### Page 3: Solid yellow - Hold on Strum (2 options)
In **Rings** and **Mini-Elements** modes (i.e. every mode except the Easter Egg mode), this option controls whether to internally sample and hold the resonator parameters (and most of the exciter parameters too) except base frequency on every trigger to STRUM. In polyphonic modes, only the active voice has its resonator parameters updated on a new trigger, meaning that each voice of polyphony can be ringing out with distinct values for Brightness, Damping, Structure, etc.
* Solid green: No hold on Strum (i.e. status quo for Rings)
* Solid red: Hold on Strum

### Page 4: Blinking green - Chord table (3 options)
In the **Easter Egg** mode and the [Western Chords](https://synthmodes.com/modules/rings/#western) resonator mode, this option controls the chord table used (“Western Chords” is not a very accurate name for some of these chord tables, but for lack of a better name I’ll continue to refer to it that way below)
* Solid green: Classic chord table
* Solid red: Alt chord table based on the one for Plaits by Jon Butler (organized a bit differently with a few more chords, based on these ones): https://github.com/lylepmills/eurorack/blob/8bd0b5e2b8088253e341ec1a50a876850e854c73/plaits/dsp/engine/chord_engine.cc#L42-L62 At higher polyphony levels (3 or 4) in the Western Chords mode, instead of chords this cycles through the first 16 standard intervals (unison to major 10th)
* Solid yellow: Alt chord tables by Joe McMullen. This mode is jam-packed with not one but five different chord table concepts depending on whether you are in the Easter Egg mode and your polyphony settings.
    * Easter Egg mode: Same table as Joe’s chord table for Plaits.
    * Western Chords (polyphony 1): Cycles through 15 scales so that Rings can be used like Martenot’s Palme Diffuseur
    * Western Chords (polyphony 2): Cycles through 21 triads, sevenths and some more exotic chords all within a single key
    * Western Chords (polyphony 3): Cycles through the first 16 harmonic intervals from the overtone series
    * Western Chords (polyphony 4): Cycles through 23 justly tempered intervals based on the Shruti of Indian classical music theory
 
For more detail, here is a longer writeup from Joe:
 
> #### Monophonic
> 
> This mode is intended to be used like a Palme Diffuseur from an Ondes
> Martenot or the resonating strings of a sitar. In other words you can
> run another voice or instrument into the audio input and use Rings
> like a tuned reverb effect. Set the frequency knob to the tonic of the
> key you are playing in, and the structure knob will tune the
> resonating ‘strings’ to these scales:
> 
> * Hungarian Minor (CCW)
> * Lydian Minor
> * Melodic Minor
> * Harmonic Minor
> * Diatonic Minor
> * Pentatonic Minor
> * Minor Triad
> * Perfect Fifths (12 O’Clock)
> * Major Triad
> * Pentatonic Major
> * Diatonic Major
> * Harmonic Major
> * Melodic Major
> * Phrygian Major
> * Byzantine Major (CW)
> 
> #### Duo-Phonic
> 
> In this mode the structure knob cycles through chords built on each
> scale degree of a diatonic key, the tonic being set by the frequency
> knob. Descending by thirds so that adjacent chords share common tones,
> the table goes through triads, sevenths, then some more exotic chords.
> For example, if frequency is set to C, structure will cycle through
> these chords:
> 
> * C
> * Am
> * F
> * Dm
> * Bo
> * G
> * Em
> * Cmaj7
> * Am7
> * Fmaj7
> * Dm7
> * Bm7b5
> * G7
> * Em7
> * Cmaj7sus4
> * Am9
> * Fmaj7#11
> * Dm11
> * Bo7
> * G7b5
> * Eaug7
> 
> #### Tri-Phonic/Quadraphonic
> 
> In these modes the structure knob cycles through justly tempered
> intervals. These modes combined with the new S&H option (@lylem you’re
> awesome) allows for the possibility of some fairly complex sequencing.
> 
> In the tri-phonic case, they are the first sixteen intervals of the
> overtone series. In other words, starting at CCW it multiplies the
> base frequency by whole numbers from 0 to 16. For the quadraphonic
> mode the structure knob cycles through the following intervals based
> on the 22 Shrutis of classical Indian music theory:
> 
> * Unison 1/1
> * Pythagorean Limma 256/243
> * Minor Diatonic Semitone 16/15
> * Minor Whole Tone 10/9
> * Major Whole Tone 9/8
> * Pythagorean Minor Third 32/27
> * Minor Third 6/5
> * Major Third 5/4
> * Pythagorean Major Third 81/64
> * Perfect Fourth 4/3
> * Acute Fourth 27/20
> * Tritone 45/32
> * Pythagorean Tritone 729/512
> * Perfect Fifth 3/2
> * Pythagorean Minor Sixth 128/81
> * Minor Sixth 8/5
> * Major Sixth 5/3
> * Pythagorean Major Sixth 27/16
> * Pythagorean Minor Seventh 16/9
> * Minor Seventh 9/5
> * Major Seventh 15/8
> * Pythagorean Major Seventh 243/128
> * Octave 2/1
> 
> #### Disastrous Peace
> 
> The chord tables in this mode are based off of what I did for the
> chords mode of Lyle’s Plaits firmware. They have been revoiced by
> hand/ear for each of the different polyphony modes. Their formulation
> is a bit esoteric, but roughly, each chord on the CW side is a
> reflection of it’s CCW counterpart, and the set covers all possible
> ’white key’ four note chord types with the addition of the fully
> diminished seventh. Like the duo-phonic set above, the frequency knob
> is meant to define a key center with the structure knob cycling
> through chords in that key. For example, if you set the frequency knob
> to C, the chords will be:
> 
> * Fm6/9
> * Abmaj7#11
> * Bb6
> * Gm11
> * Ebadd4
> * Cmaddb13
> * Abadd#11
> * Fm6
> * Do7
> * Bo7
> * G7
> * Em/F
> * Cmaj7
> * Am9
> * Fmaj9
> * Dm7
> * Cmaj7sus4
> * G7sus4

### Page 5: Blinking red - FM CV input destination (2 options)
In **Mini-Elements** modes only, this option controls whether to use the FM CV input to control exciter Timbre. This option has no effect on other modes.
* Solid green: FM CV input controls FM (i.e. status quo for Rings)
* Solid red: FM CV input controls exciter timbre in mini-Elements modes

## Mini-Elements mode
The Mini-Elements modes are, of course, based on [Elements](https://mutable-instruments.net/modules/elements/). They directly borrow some of the extra functionality of Elements that Rings lacks, in particular the exciter section of Elements. This gives Rings a wider palette of options for internal excitation compared with the stock firmware. Given the much more limited panel real estate on Rings compared with Elements, obviously a few compromises had to be made, chiefly that you can only use one exciter type at a time (Bow, Blow, or Strike) and there is no Envelope control.

For all exciter types, the “Timbre” control from Elements is controlled by the FREQUENCY knob when frequency is locked. For the Blow and Strike exciter types, the POSITION knob on Rings becomes the “meta” knobs for controlling those exciters (labeled Flow and Mallet on Elements), and no longer control the Position parameter in the resonator (instead Position gets set to a reasonable default for the given resonator type).

In the stereo Mini-Elements mode, the exciter is fed into the resonator internally. In the Mini-Elements + Exciter mode, it is not (so you can envelope it or do whatever processing you want before patching it back to IN).

Like with Elements, using the Strike exciter type requires triggers/gates to the STRUM input in order to activate the exciter. By contrast Bow and Blow will sound continuously, making Rings behave something more like a complex oscillator than a pure resonator in the stereo Mini-Elements mode.

Rings’s normal internal exciter is not disabled by default in Mini-Elements mode. If you want to remove the sound of the classic internal exciter for Rings, just patch a dummy cable into the IN jack.

## Technical notes
*On the internal sample rate* - In order to avoid overtaxing the processor in several of the new modes, the internal sample rate of the module has been reduced from 48K to 32K (the same as Elements). I.e. if you really like frequencies above 16kHz, this may not be the firmware for you.

*On the usage of Odd/Even outputs* - As you probably know, the ODD/EVEN outputs behave differently in Rings depending on whether one or both is patched. Basically the way it works is that the EVEN output gets summed into ODD unless EVEN is patched (this is why if only one output is used, people generally use ODD). This happens entirely in hardware and can’t be accounted for in the firmware. The reason this is worth noting here is that in the Rings + Waveform and Mini-Elements + Exciter modes, the EVEN output is a waveform or raw exciter signal, meaning that if EVEN is unpatched in those modes, its signal will be summed into ODD, which might sound strange/bad. The takeaway is that when using those modes, EVEN should pretty much always be patched.

*On the internal slewing of CV inputs* - In the official firmware the Structure, Damping, Brightness, and Position CV inputs have a slight slew applied to them internally. To better support the Hold on Strum mode, this internal slew has been removed, so you may notice the CV inputs are somewhat more sensitive now to CV signals that change at very fast rates.
