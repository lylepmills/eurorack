# Plaits Lab package catalog

`catalog.json` is the source of truth for every approved stock and Rubato model.
It drives the firmware registry generator, SDK forking, build allowlist, and the
generated web catalog. Do not maintain independent engine lists in those
consumers.

The top-level `manuals` map holds user-facing control and trigger prose for
every engine. It is exported with a separate documentation digest, so wording
and layout improvements do not invalidate firmware package references.

Run `python3 alt_firmwares/plaits_lab_catalog/validate_catalog.py` after editing
the catalog. The validator checks identifiers, source files, implementation
metadata, package uniqueness, presets, and content digests.
