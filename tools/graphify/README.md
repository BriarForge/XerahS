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
