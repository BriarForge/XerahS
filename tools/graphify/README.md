# Scoped Graphify hooks

The repository has independent graphs under `platforms/linux`, `platforms/macos`,
`platforms/windows`, and `product-contract`. Git invokes hooks at the repository
root, so repeating `graphify hook install` in these folders installs the same
repository hook rather than four independently scoped hooks.

After the native installs, install the additional dispatcher from the root:

```powershell
python tools/graphify/scoped_hooks.py install
```

It preserves existing hooks and adds a marked block before their early exits.
Commits, merges and branch checkouts update affected scoped code graphs;
rewrites conservatively refresh all four. Graph output and installed skill changes
do not trigger a rebuild. Git identity wrappers can be selected explicitly with
`XERAHS_GIT_WRAPPER`; otherwise the configured Git identity is used.

Updates run in a hidden background process on Windows, with output in
`.git/graphify-scoped-hooks.log`. Existing Graphify locking protects concurrent
updates. A foreground refresh before answering remains mandatory:

```powershell
cd platforms/linux
graphify update .
```

Graphify's update CLI and hooks refresh code/AST relationships. They cannot ask a
host agent to semantically read new prose. A documentation change leaves
`graphify-out/.needs_update`; run the Graphify skill's `$graphify . --update` from
that scope once at the end of the task, not after each checkpoint. Generated
`PROGRESS.md` files are listed in `.graphifyignore` and never need that refresh.
Rewrites inspect all scopes; semantic
hashes prevent a freshly built graph committed with its docs from being marked
stale again. The dispatcher never claims cross-platform parity.

Inspect routing without changing a graph, or refresh all scopes synchronously:

```powershell
python tools/graphify/scoped_hooks.py --all --dry-run
python tools/graphify/scoped_hooks.py --all
python -m unittest discover tools/graphify/tests -v
```

Graphs, reports, and HTML are versioned. Caches, interpreter paths, manifests,
cost counters and other hidden Graphify state stay local. Run the four scoped
`graphify install --project --platform codex` and `graphify hook install` commands
again in a new clone, then install this dispatcher. The project install appends a
`## graphify` section to each scope's `AGENTS.md`; delete it, because the root
`AGENTS.md` already governs every scope. Do not commit caches or
machine-specific hook scripts from `.git`.

## Semantic fragment origin

Prepare semantic-only fragments before saving the semantic cache or passing them
to Graphify's builder. The tracked Graphify skill includes this step for fresh
extractions and for the combined cached/fresh semantic fragment:

```sh
python tools/graphify/semantic_origin.py /path/to/semantic.json /path/to/semantic.json
```

The helper gives every semantic node, edge and hyperedge an explicit
`_origin: "semantic"` while preserving IDs, source locations, confidence and
coverage. It operates on an owned copy and replaces the destination atomically.
Invalid input and explicit AST provenance leave an existing output untouched.
Never pass the combined AST-plus-semantic extraction to this helper.

This fixes a producer ambiguity exposed by the Linux graph: semantic nodes had
precise `L17`-style locations but omitted `_origin`. Graphify's legacy fallback
classified them as AST, so loading the graph could merge older semantic records
into those falsely classified nodes. Its preservation guard then correctly
rejected the apparent loss. A location says where evidence is; it does not say
which extractor produced it. Keep the guard enabled. Existing affected graphs
need a one-time provenance repair using their verified original semantic
fragment; this helper does not relabel saved graphs or guess node provenance.

Verify the fragment preparation and hook dispatcher with:

```sh
python -m unittest discover tools/graphify/tests -v
```

## Code-only refresh runtime fix

The affected Graphify runtime also deduplicates saved semantic records during a
code-only rebuild. Two separately recorded decisions with the same source and
label can then collapse, and the preservation guard correctly refuses the write.
Code refresh must preserve those identities; semantic extraction decides when
semantic records should be reconciled.

Apply the narrow, idempotent patch using Graphify's installed Python interpreter,
from the repository root:

```sh
/path/to/graphify/python tools/graphify/runtime_patch.py
```

This adds `dedup=False` only to the runtime's code-refresh graph construction.
It leaves extraction and all preservation checks enabled. The script refuses an
unrecognized implementation instead of editing arbitrary package code. Reapply
after reinstalling an affected Graphify release. The end-to-end regression runs
ordinary `graphify update .` twice and verifies that distinct same-named semantic
decisions and their rationale survive.
