# String Autopsy

A diagnostic, not a musical model: a byte-faithful copy of the stock
inharmonic-string engine with per-section cycle bracketing, built to localize an
unexplained 4x cost overrun measured on hardware. Build with
`build --hardware --cpu-probe`; the AUX readout cycles through identity-beacon
reports (1 burst = whole render, 2-4 = sections 0..2 = engine total and the
three string voices... see src/string_autopsy_engine.cc for the section map).
