#!/usr/bin/env bash
# Regenerate (or verify) public_catalog.json — the builder Worker's engine
# allowlist — from the canonical exporter at a CLEAN committed source ref.
#
# WHY THIS EXISTS: public_catalog.json is a generated artifact that the Worker
# imports statically as its digest allowlist (see plaits_lab_builder/src/
# contract.ts). Each engine's digest hashes its catalog METADATA record plus its
# source bytes, so even a display-name-only edit (e.g. the DX7->6-Op FM rename in
# a4219d1) moves a digest. If public_catalog.json is not regenerated after such a
# change, the deployed Worker rejects the very recipes the website emits with
# "The recipe contains an unavailable package version." — which is exactly how
# every stock build silently broke once. This script makes regeneration
# reproducible and gives `pnpm deploy` a freshness gate (predeploy -> --check).
#
# It regenerates from `git archive <ref>` (default HEAD), NOT the working tree,
# because export_web_catalog.py hashes source files from the repo root — a dirty
# tree would bake in-progress engine edits into the shipped digests.
#
# Usage:
#   ./sync_public_catalog.sh              # regenerate public_catalog.json @ HEAD
#   ./sync_public_catalog.sh --check      # fail if committed file drifts from HEAD
#   ./sync_public_catalog.sh --ref <rev>  # use <rev> instead of HEAD
set -euo pipefail

CATALOG_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$CATALOG_DIR/../.." && git rev-parse --show-toplevel)"
OUT="$CATALOG_DIR/public_catalog.json"

REF="HEAD"
CHECK=0
while [ $# -gt 0 ]; do
  case "$1" in
    --check) CHECK=1; shift ;;
    --ref) REF="$2"; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

COMMIT="$(git -C "$REPO_ROOT" rev-parse "$REF")"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# Full tree at the pinned commit — the exporter's digests hash sources under
# plaits/, not just the catalog dir.
git -C "$REPO_ROOT" archive "$COMMIT" | tar -x -C "$TMP"
python3 "$TMP/alt_firmwares/plaits_lab_catalog/export_web_catalog.py" "$TMP/public_catalog.generated.json" >/dev/null

if [ "$CHECK" -eq 1 ]; then
  if ! diff -q "$OUT" "$TMP/public_catalog.generated.json" >/dev/null 2>&1; then
    echo "ERROR: public_catalog.json is STALE vs source ${COMMIT:0:12}." >&2
    echo "       The builder allowlist would reject recipes the exporter/website emit." >&2
    echo "       Regenerate + commit:  ./sync_public_catalog.sh${REF:+ --ref $REF}" >&2
    echo >&2
    echo "Drifting engines (committed digest -> source digest):" >&2
    python3 - "$OUT" "$TMP/public_catalog.generated.json" >&2 <<'PY'
import json, sys
def m(p):
    c = json.load(open(p)); es = c.get("engines", c)
    return {e["id"]: e["digest"] for e in es}
old, new = m(sys.argv[1]), m(sys.argv[2])
for k in sorted(set(old) | set(new)):
    if old.get(k) != new.get(k):
        print(f"  {k}: {str(old.get(k))[7:19]} -> {str(new.get(k))[7:19]}")
PY
    exit 1
  fi
  echo "public_catalog.json is up to date with source ${COMMIT:0:12}"
  exit 0
fi

cp "$TMP/public_catalog.generated.json" "$OUT"
echo "wrote public_catalog.json from source ${COMMIT:0:12}"
