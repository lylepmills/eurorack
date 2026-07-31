# Renaissance Scrub Prototype

An interaction prototype built entirely from Plaits' MIT-licensed LPC speech
synthesizer and number-word bank. It does not contain Renaissance/SoftVoice SAM
tables or word data.

The point of this model is to test Renaissance's control split beside stock
Speech on hardware:

- **HARMONICS — Word:** zero through ten.
- **TIMBRE — Position:** holds a frame inside the selected word. A trigger plays
  forward from this position, then returns to the held frame.
- **MORPH — Formant:** vocal-tract size.
- **MACRO — Rate:** playback speed, with the source rate at noon.
- **MAIN:** LPC speech. **AUX:** the LPC excitation.

From the eurorack checkout, validate it with:

    python3 alt_firmwares/plaits_lab_sdk/plaits_lab.py check \
      alt_firmwares/plaits_lab_sdk/packages/community/renaissance-scrub-prototype --full

Build a two-model hardware firmware, with this prototype first and unchanged
stock Speech second:

    python3 alt_firmwares/plaits_lab_sdk/plaits_lab.py build \
      alt_firmwares/plaits_lab_sdk/packages/community/renaissance-scrub-prototype \
      --hardware --compare-with speech --output /tmp/renaissance-speech-ab.wav
