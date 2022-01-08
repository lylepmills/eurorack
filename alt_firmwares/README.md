# Plaits

The Plaits alt firmware ([plaits_freqlock.wav](https://github.com/lylepmills/eurorack/raw/master/alt_firmwares/plaits_freqlock.wav)) extends the base firmware with several additional features. Unlike earlier versions of this alt firmware, you can now use any combination of the below features that you want with this single firmware, and you can reconfigure those options with a built-in menu.

- **Frequency locking.** It is now possible to lock the frequency of the oscillator, so you can avoid accidentally detuning it once you've tuned it where you want it. When locked, the main frequency knob will no longer affect the frequency of the oscillator, allowing you to keep the module in tune more easily (the V/Oct and FM inputs still work as normal, as does the FM attenuverter).
- **Frequency knob alt functionality.** When frequency is locked, the frequency knob can control something else. This alt firmware supports two options for what it controls. It can become either an octave switcher or control a crossfade between the main and aux synthesis models on the aux output.
- **MODEL input alt functionality.** The MODEL input can be repurposed to control aux crossfade with CV. This can be used with or without the frequency knob alt functionality controlling aux crossfade.
- **LEVEL input alt functionality.** The LEVEL input can be repurposed to control the decay of the internal envelope with CV. This will only take effect if TRIG is patched, otherwise LEVEL will control the internal VCA/VCFA as it normally would.
- **Suboscillator.** The aux model can be replaced by a square or sine wave suboscillator. The square wave can be used to sync another oscillator with a sync input (like Tides), an oscilloscope, etc. This is fully compatible with aux crossfading (either via the frequency knob alt functionality, or via the MODEL cv input alt functionality), meaning you can blend the main model with a square or sine wave. There are also options for the suboscillator frequency to be either the same frequency as the main model, -1 octave, or -2 octaves.
- **Alternate chord table.** - For the chord oscillator mode (the 7th green mode), you can switch between the original table of 11 chords, or an alternative chord table by Jon Butler with 17 chords organized somewhat differently.

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
Green means the original chord table, red means the alternate chord table by Jon Butler


