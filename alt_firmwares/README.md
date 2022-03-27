# Plaits

[discussion](https://forum.mutable-instruments.net/t/unified-plaits-alt-firmware/19530)

[download](https://github.com/lylepmills/eurorack/raw/master/alt_firmwares/plaits_freqlock.wav)

*update 3/27/22: versions of this firmware downloaded prior to 3/27/22 are known to have had some issues with hitting CPU limits of the module, producing digital aliasing distortions in certain modes with certain combinations of settings. updating to the latest version of the firmware is recommended to avoid running into those problems.*

The Plaits alt firmware extends the base firmware with several additional features. Unlike earlier versions of this alt firmware, you can now use any combination of the below features that you want with this single firmware, and you can reconfigure those options with a built-in menu.

## Features of this alt firmware

- **Frequency locking.** It is now possible to lock the frequency of the oscillator, so you can avoid accidentally detuning it once you've tuned it where you want it. When locked, the main frequency knob will no longer affect the frequency of the oscillator, allowing you to keep the module in tune more easily (the V/Oct and FM inputs still work as normal, as does the FM attenuverter).
- **Frequency knob alt functionality.** When frequency is locked, the frequency knob can control something else. This alt firmware supports three options for what it controls. It can become either control a crossfade between the main and aux synthesis models on the aux output, or do transposition by octaves, or fifths.
- **MODEL input alt functionality.** The MODEL input can be repurposed to control aux crossfade or LPG colour with CV. The aux crossfade option can be used with or without the frequency knob alt functionality controlling aux crossfade.
- **LEVEL input alt functionality.** The LEVEL input can be repurposed to control the decay of the internal envelope with CV. This will only take effect if TRIG is patched, otherwise LEVEL will control the internal VCA/VCFA as it normally would.
- **Suboscillator.** The aux model can be replaced by a square or sine wave suboscillator on the aux output. The square wave can be used to sync another oscillator with a sync input (like Tides), an oscilloscope, etc. This is fully compatible with aux crossfading (either via the frequency knob alt functionality, or via the MODEL cv input alt functionality), meaning you can blend the main model with a square or sine wave. There are also options for the suboscillator frequency to be either the same frequency as the main model, -1 octave, or -2 octaves.
- **Alternate chord table.** For the chord oscillator mode (the 7th green mode), you can switch between the original table of 11 chords, or alternative chord tables by Jon Butler (17 chords) or Joe McMullen (18 chords). See below for more details on each chord table.

## How to toggle frequency locking
To lock or unlock frequency, briefly hold both buttons at once until all the lights flash up either green or red. Green means that frequency is unlocked, and red means it is locked.

## How to access calibration
The calibration process is the same as the base firmware, except you must long press (about 5 seconds) both buttons *starting with the one on the right* to access the calibration mode.

## How to change the other options
To access the menu of options for the various alt functionalities, long press (about 5 seconds) both buttons *starting with the one on the left*. To exit the menu, press both buttons at once.

The menu is represented by lights for each option. You can navigate between the menu items by pressing the left button, and you can switch the option for that item by pressing the right button. The current settings are represented by the color of the LED lights. In order, they represent the following

### First light - Frequency knob alt functionality (when frequency is locked)
Green means aux crossfade, red means transposition by octaves, yellow means transposition by fifths

### Second light - MODEL input alt functionality
Green means model (as in the stock firmware), red means aux crossfade, yellow means LPG colour (VCFA->VCA)

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
With this table, instead of Frequency defining the root and Harmonics defining chord type, Frequency defines the key and Harmonics defines the scale position the chord is built from, meaning the root you hear on the aux output shifts around. Note that chord voicings are also more spread out by default with this table. See https://forum.mutable-instruments.net/t/unified-plaits-alt-firmware/19530/19 for more discussion. If that doesn't make sense, don't worry, just use your ears!
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

## Technical notes
The original Plaits firmware makes the most of its processor, and runs quite close to that processor's limits in terms of both the amount of code it can store, and the CPU load of that code (at least, in certain modes). As such, to pack in everything this alt firmware can do, several minor adjustments have been made to the behavior of the base firmware for efficiency reasons.

- The size of the sine lookup-table (used in several modes) has been halved (max error 2e-5) (https://forum.mutable-instruments.net/t/unified-plaits-alt-firmware/19530/7 for more details)
- The internal sample rate for the audio path has been dropped from 48K to 44.1K
- The crossfading behavior between the string synth and the wavetable synth in chord mode right around 12 o'clock on the morph knob has been tweaked such that each note fades completely out and then back in again one at a time (instead of crossfading between the two synths)

When it comes to CPU usage, various things contribute, e.g. using the suboscillator adds a bit of CPU, using the trig input + internal envelopes adds a bit of CPU, aux crossfading adds a bit of CPU, etc. So far I have been able to make enough performance tweaks to address all combinations of settings which can lead to CPU overload *that I know of*. In other words, at time of writing (3/27/22), any combination of settings *should* work fine without hitting CPU limits, but fair warning: that isn't a guarantee that no more such scenarios will be found in the future.

# Rings

[discussion](https://forum.mutable-instruments.net/t/mini-elements-alt-firmware-for-rings/19768)

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
* Solid yellow: Alt chord tables by Joe McMullen. This mode is jam-packed with not one but five different chord table concepts depending on whether you are in the Easter Egg mode and your polyphony settings. Refer to [Joe's writeup](https://forum.mutable-instruments.net/t/mini-elements-alt-firmware-for-rings/19768/7) for more details.
    * Easter Egg mode: Same table as Joe’s chord table for Plaits.
    * Western Chords (polyphony 1): Cycles through 15 scales so that Rings can be used like Martenot’s Palme Diffuseur
    * Western Chords (polyphony 2): Cycles through 21 triads, sevenths and some more exotic chords all within a single key
    * Western Chords (polyphony 3): Cycles through the first 16 harmonic intervals from the overtone series
    * Western Chords (polyphony 4): Cycles through 23 justly tempered intervals based on the Shruti of Indian classical music theory

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
