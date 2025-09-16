#!/usr/bin/env bash
set -euo pipefail

# Usage:
#   BIN=./dsd_inspector ./dsd_tree_to_html_central.sh "<ROOT>" [OUTROOT]
# Example:
#   BIN=./dsd_inspector ./dsd_tree_to_html_central.sh "/media/manolo/G/5.Music/SACD-R ISO/"

ROOT="${1:-/media/manolo/G/5.Music/SACD-R ISO/}"
OUTROOT="${2:-./dsd_inspector_out}"   # single central output folder (created if missing)
BIN="${BIN:-./dsd_inspector}"         # your compiled binary
FORCE="${FORCE:-0}"                   # set FORCE=1 to re-run even if images exist
INCLUDE_DFF="${INCLUDE_DFF:-0}"       # set to 1 to include *.dff too

mkdir -p "$OUTROOT"

# Normalize ROOT to absolute for stable relpaths
if command -v realpath >/dev/null 2>&1; then
  ROOT_ABS="$(realpath -m "$ROOT")"
  OUTROOT_ABS="$(realpath -m "$OUTROOT")"
else
  # crude fallbacks
  ROOT_ABS="$ROOT"
  OUTROOT_ABS="$OUTROOT"
fi

# HTML path (in OUTROOT)
HTML="$OUTROOT_ABS/index.html"

# Helper: relative path ROOT→target (fallback if realpath lacks --relative-to)
relpath() {
  if command -v realpath >/dev/null 2>&1; then
    realpath --relative-to="$ROOT_ABS" "$1" 2>/dev/null && return 0
  fi
  case "$1" in
    "$ROOT_ABS"*) printf '%s\n' "${1#$ROOT_ABS/}";;
    *) printf '%s\n' "$1";;
  esac
}

# Start HTML
{
  printf '%s\n' '<!doctype html>'
  printf '%s\n' '<html lang="en"><meta charset="utf-8">'
  printf '<title>%s</title>\n' 'DSD Inspector — summary'
  cat <<'CSS'
<style>
  body { font-family: system-ui, sans-serif; margin: 24px; background:#0b0b0b; color:#f0f0f0; }
  h1 { margin: 0 0 12px 0; font-weight: 600; }
  .meta { color:#aaa; font-size: 12px; margin-bottom: 6px; }
  .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(480px, 1fr)); gap: 24px; }
  .card { background:#131313; border:1px solid #2a2a2a; border-radius:14px; padding:16px; }
  .card h2 { font-size: 16px; margin: 0 0 6px 0; font-weight:600; word-break: break-all; }
  .imgs { display:flex; gap:12px; flex-wrap:wrap; }
  .imgs img { max-width: 100%; height:auto; border:1px solid #2a2a2a; border-radius:10px; }
  a { color:#88c9ff; text-decoration:none; }
  a:hover { text-decoration:underline; }
  code { color:#ddd; }
</style>
CSS
  printf '<h1>%s</h1>\n' 'DSD Inspector — summary'
  printf '<p>Scanned root: <code>%s</code></p>\n' "$ROOT_ABS"
  printf '%s\n' '<div class="grid">'
} >"$HTML"

# Build the find expression
if [[ "$INCLUDE_DFF" -eq 1 ]]; then
  FIND_EXPR=( \( -iname '*.dsf' -o -iname '*.dff' \) )
else
  FIND_EXPR=( -iname '*.dsf' )
fi

# Loop over unique directories that contain at least one target file
while IFS= read -r -d '' DIR; do
  # choose the first file in that folder
  if [[ "$INCLUDE_DFF" -eq 1 ]]; then
    FILE="$(find "$DIR" -maxdepth 1 -type f \( -iname '*.dsf' -o -iname '*.dff' \) | head -n 1)"
  else
    FILE="$(find "$DIR" -maxdepth 1 -type f -iname '*.dsf' | head -n 1)"
  fi
  [[ -n "$FILE" ]] || continue

  REL_DIR="$(relpath "$DIR")"
  OUTDIR="$OUTROOT_ABS/$REL_DIR"
  mkdir -p "$OUTDIR"

  # Run analyzer into OUTDIR (central; nothing touches music folders)
  if [[ "$FORCE" -eq 1 || ! -s "$OUTDIR/spectrum_overlay.png" ]]; then
    echo ">>> Processing: $FILE"
    "$BIN" -i "$FILE" --out "$OUTDIR" || echo "   [WARN] failed: $FILE" >&2
  fi

  # Read classification (if present)
  CLS=""
  if [[ -s "$OUTDIR/report.txt" ]]; then
    CLS="$(grep -m1 '^Classification:' "$OUTDIR/report.txt" | cut -d: -f2- | sed 's/^[[:space:]]*//')"
  fi

  BASE="$(basename "$FILE")"
  REL_OVER="$(relpath "$OUTDIR")/spectrum_overlay.png"
  REL_SPEC_PRETTY="$(relpath "$OUTDIR")/spectrogram_pretty.png"
  REL_REP="$(relpath "$OUTDIR")/report.txt"
  REL_SPEC_RAW="$(relpath "$OUTDIR")/spectrogram.png"
  REL_SPECTRUM_AVG="$(relpath "$OUTDIR")/spectrum_avg.png"

  # Write card
  {
    printf '%s\n' '<div class="card">'
    printf '<h2>%s</h2>\n' "$BASE"
    printf '<div class="meta">%s</div>\n' "$REL_DIR"
    if [[ -n "$CLS" ]]; then
      printf '<p><strong>Classification:</strong> %s</p>\n' "$CLS"
    fi
    printf '%s\n' '<div class="imgs">'
    [[ -s "$OUTDIR/spectrum_overlay.png" ]]   && printf '<img src="%s" alt="spectrum_overlay for %s">\n' "$REL_OVER" "$BASE"
    [[ -s "$OUTDIR/spectrogram_pretty.png" ]] && printf '<img src="%s" alt="spectrogram_pretty for %s">\n' "$REL_SPEC_PRETTY" "$BASE"
    printf '%s\n' '</div>'
    printf '<p><a href="%s">report.txt</a>' "$REL_REP"
    [[ -s "$OUTDIR/spectrogram.png" ]]   && printf ' &middot; <a href="%s">spectrogram.png</a>' "$REL_SPEC_RAW"
    [[ -s "$OUTDIR/spectrum_avg.png" ]]  && printf ' &middot; <a href="%s">spectrum_avg.png</a>' "$REL_SPECTRUM_AVG"
    printf '%s\n' '</p>'
    printf '%s\n' '</div>'
  } >>"$HTML"

done < <(
  # list directories that contain at least one matching file
  find "$ROOT_ABS" -type f "${FIND_EXPR[@]}" -printf '%h\0' | sort -zu
)

# Close HTML
printf '%s\n' '</div></html>' >>"$HTML"

echo
echo "All outputs are in:"
echo "  $OUTROOT_ABS"
echo "Open the summary:"
echo "  $HTML"
