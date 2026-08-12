# Wavetable Bank Transport Bench

Temporary physical-module diagnostic for the Plaits Palette wavetable work.
It exercises the intended variable-bank HARMONICS transport with duplicated
mirror endpoints. Every bank has a distinct equation-generated spectrum.

MACRO selects one of six configurations:

1. one bank, mirrored;
2. two banks, mirrored;
3. three banks, mirrored;
4. eight banks, mirrored;
5. eight banks, one way;
6. sixteen banks, one way.

HARMONICS traverses the selected path, while TIMBRE and MORPH remain the two
coordinates within each 8 x 8 bank. Build with `--cpu-probe-aux` so OUT carries
the musical signal and AUX carries the physical CPU measurement tone.
