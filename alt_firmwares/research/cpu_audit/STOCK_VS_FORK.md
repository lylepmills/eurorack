Stock vs fork overhead, measured on the module 2026-09-25
===========================================================

Same module, same ES-8 patch, same eight fixed scenes, same cost bracket
(DWT, audio interrupt entry to the end of Voice::Render), regular AUX.

- stock_baseline_2026-09-25.json: upstream Mutable Plaits at ebe6bbf6 (the
  last upstream commit), with only a timing graft (stock_baseline_graft.patch,
  applies to ebe6bbf6 with stmlib at e3bd7c9: three marks + the round-robin UI
  task; build with `make -f plaits/makefile RESOURCES= wav`) and the three 6-op FM slots pointed at
  another engine so the graft fits in flash. No scene uses FM.
- fork_matched_2026-09-25.json: this fork at 737b746e, the same engines by
  catalog id, TWIST at its neutral centre, a --stereo recipe (per-engine stereo
  paths compiled in, which costs a few percent even in mono), eleven marks.

Worst block, fraction of the 12-sample period:

  scene                   stock   fork   delta
  VA baseline             0.715   0.798  +0.083
  VA + TRIG               0.790   0.871  +0.081
  Modal typical           0.943   1.000  +0.057
  Modal mono worst        0.953   1.013  +0.060
  Chords typical + TRIG   0.952   1.012  +0.060
  Chords TIMBRE=1 + TRIG  0.976   1.043  +0.067
  String struck           0.989   0.999  +0.010
  Swarm + TRIG            0.944   1.002  +0.058
  (model select, stock)   up to 1.54 -- stock overruns on engine switches too

Engines cost the same in both (Modal 0.759 vs 0.750, Chords 0.736 vs 0.728).
The difference is the fork's per-block overhead, ~0.05 after allowing ~0.01
for the fork's extra timing marks: UI +0.025 (UpdateLEDs 0.042 vs 0.026; the
fork builds ui.o at -Os, stock at -O2, and -O2 costs 4.5 KB the stock palette
does not have), Voice setup +0.027, output stage +0.005.

Stock itself leaves Modal/Chords/String 1-4% headroom, so the fork's overhead
is what pushes them over the ~0.985 onset (threshold_ladder_2026-09-24.json).
