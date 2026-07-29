# Digital Modulation

A port of Braids' QPSK model: a carrier driven by a framed packet of dibits
through a four-point constellation.

The DSP is Emilie Gillet's `DigitalOscillator::RenderDigitalModulation`.
Braids has two knobs here and neither is a frame control — parameter_[0] is
the symbol rate, parameter_[1] the payload byte, and the packet is welded to
1,088 symbols with its preamble and two sync words at 32, 48 and 64.
HARMONICS opens the frame and MORPH shapes the symbol transitions; both are
new, and MORPH at zero is Braids' hard-switched constellation.

**MACRO is Braids' payload knob, quantised the way the module quantises it.**
The knob is an int16 0..32767, a whole-number one-pole chases it, and the
packet reads only the top 8 bits — and that byte *is* the four-symbol dibit
pattern, so one step of it is a different transmission rather than a slightly
different tone. The engine runs the same integers. The detent is the module's
noon, byte 127.

**Frame length is on HARMONICS deliberately.** On MACRO it would sit at the
detent by default, pinning the stock 1,088-symbol frame — about 211 seconds at
MIDI 36 with TIMBRE down — leaving the control inert for the whole header.

A trigger restarts the packet at its preamble and resets nothing else — the
carrier, the payload filter and the byte in flight all run on, so the first
three symbols after a strike are the tail of the byte that was interrupted.
You only hear that when the packet has got past its 32-symbol preamble since
the last strike; at the slow symbol rates this engine spends most of its range
in, a strike lands mid-preamble and there is no tail to carry.

AUX is the symbol staircase, and it is honestly a modulation source rather
than a second voice: at MIDI 36 with TIMBRE down it is a ~5 Hz stepped LFO,
becoming a voice only at high TIMBRE and mid-to-high pitch. It sits at +1.0
for the entire preamble, hence the DC blocker and the negative gain.

Both copyright lines are carried in `LICENSE` and in each source file.
