#!/bin/bash
# Compare two folders of renders produced by render_all.sh.
#   ./compare_shots.sh out/base320 out/now320
# Used to prove a UI change did NOT move anything at a size it should not
# have touched -- the narrow panels, when only the wide one was meant to grow.
cd "$(dirname "$0")"
A="$1"; B="$2"
same=0; diff=0
for f in "$A"/*.png; do
  n=$(basename "$f")
  if cmp -s "$f" "$B/$n"; then
    same=$((same+1))
  else
    diff=$((diff+1)); echo "  DIFFERS: $n"
  fi
done
echo "$A vs $B: $same identical, $diff differ"
