#!/usr/bin/env bash
# Integration gate: put worker commits on top of origin/main, regenerate, verify, push.
#
#   gate.sh [--no-push] [--wrapper git-aoife] [--remote aoife] <commit>...
#
# Run from the orchestrator's detached worktree (clean tree required).
# - Cherry-picks each commit in order onto a fresh origin/main.
# - Conflicts only in generated files (platforms/**/PROGRESS.md, platforms/linux/graphify-out/**)
#   are resolved by keeping origin/main's copy and regenerating afterwards. Any other conflict
#   stops the gate with the cherry-pick left in progress for manual resolution
#   (never `git add -A` a conflicted tree: it stages the markers).
# - Regenerates progress and the Linux graph, runs the progress check and the contract linter,
#   builds linux-qt and runs the whole CTest suite. A failing test is rerun alone once
#   (known flaky under parallel load: editor-window-*, workflow-shortcuts); a second failure stops.
# - Commits regenerated files, re-fetches, refuses to push if origin/main moved, then pushes.
set -euo pipefail

push=1; wrapper=git-aoife; remote=aoife
while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-push) push=0; shift ;;
    --wrapper) wrapper="$2"; shift 2 ;;
    --remote) remote="$2"; shift 2 ;;
    *) break ;;
  esac
done
(( $# > 0 )) || { echo "usage: gate.sh [--no-push] <commit>..." >&2; exit 2; }

root="$(git rev-parse --show-toplevel)"
cd "$root"
[[ -z "$(git status --porcelain)" ]] || { echo "gate: worktree not clean" >&2; exit 1; }
generated='^platforms/(.*/)?PROGRESS\.md$|^platforms/linux/graphify-out/'

git fetch -q origin
git checkout -q --detach origin/main
base="$(git rev-parse HEAD)"
for c in "$@"; do
  if ! git cherry-pick "$c" >/dev/null 2>&1; then
    mapfile -t unmerged < <(git diff --name-only --diff-filter=U)
    other="$(printf '%s\n' "${unmerged[@]}" | grep -Ev "$generated" || true)"
    if [[ -n "$other" ]]; then
      echo "gate: code conflict while applying $c; resolve by hand, then rerun from that commit:" >&2
      printf '  %s\n' $other >&2; exit 1
    fi
    git checkout --ours -- "${unmerged[@]}"
    git add -- "${unmerged[@]}"
    GIT_EDITOR=true git cherry-pick --continue >/dev/null
  fi
  echo "gate: applied $c -> $(git rev-parse --short HEAD)"
done

python tools/platform-progress/update.py >/dev/null
(cd platforms/linux && graphify update . >/dev/null 2>&1) || echo "gate: graphify update failed (continuing)"
python tools/platform-progress/update.py --check
python tools/contract-linter/lint.py | tail -n 1

qt=platforms/linux/linux-qt
cmake -S "$qt" -B "$qt/build" -G Ninja -DCMAKE_BUILD_TYPE=Debug >/dev/null
cmake --build "$qt/build" -j"$(( $(nproc) / 2 ))" >/dev/null
if ! ctest --test-dir "$qt/build" -j4 >/tmp/gate-ctest.$$ 2>&1; then
  # CTest lists failures as "<tab> NN - name (Failed)"; match any leading whitespace.
  mapfile -t failed < <(sed -nE 's/^[[:space:]]*[0-9]+ - ([^ ]+) \(.*/\1/p' /tmp/gate-ctest.$$)
  if (( ${#failed[@]} == 0 )); then
    echo "gate: ctest failed but no failing test names were parsed:" >&2; tail -n 20 /tmp/gate-ctest.$$ >&2; exit 1
  fi
  for t in "${failed[@]}"; do
    echo "gate: rerunning $t alone"
    ctest --test-dir "$qt/build" -R "^${t}\$" --output-on-failure || { echo "gate: $t failed twice" >&2; exit 1; }
  done
fi
grep -E 'tests passed' /tmp/gate-ctest.$$ || true; rm -f /tmp/gate-ctest.$$

if [[ -n "$(git status --porcelain)" ]]; then
  git add platforms/PROGRESS.md platforms/*/PROGRESS.md platforms/linux/graphify-out
  [[ -z "$(git status --porcelain | grep -v '^M ' || true)" ]] || { echo "gate: unexpected changes:" >&2; git status --short >&2; exit 1; }
  "$wrapper" commit -q -m "[Linux] Regenerate progress and graph after integration

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
fi

(( push )) || { echo "gate: verified $(git rev-parse --short HEAD) (not pushed)"; exit 0; }
git fetch -q origin
[[ "$(git rev-parse origin/main)" == "$base" ]] || { echo "gate: origin/main moved during the gate; rerun" >&2; exit 1; }
"$wrapper" push "$remote" HEAD:main
echo "gate: pushed $(git rev-parse --short HEAD)"
