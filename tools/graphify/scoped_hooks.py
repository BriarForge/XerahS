"""Route repository-wide Git hooks to XerahS's independent Graphify graphs."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys

SCOPES = ('platforms/linux', 'platforms/macos', 'platforms/windows', 'product-contract')
START = '# xerahs-scoped-graphify-start'
END = '# xerahs-scoped-graphify-end'
EVENTS = ('post-commit', 'post-checkout', 'post-merge', 'post-rewrite')


def git(*args: str, root: Path) -> bytes:
    # An explicit identity wrapper can be supplied; never guess a person's identity.
    command = os.environ.get('XERAHS_GIT_WRAPPER', 'git')
    return subprocess.check_output([command, *args], cwd=root)


def changed_files(root: Path, event: str, args: list[str]) -> list[str] | None:
    """None means all scopes; paths are NUL-delimited to preserve unusual names."""
    if event == 'post-checkout':
        if len(args) < 3:
            raise ValueError('post-checkout requires old revision, new revision, branch flag')
        if args[2] == '0':
            return []
        revisions = args[:2]
    elif event == 'post-merge':
        revisions = ['ORIG_HEAD', 'HEAD']
    elif event == 'post-rewrite':
        return None
    else:
        data = git('diff-tree', '--root', '-m', '--no-commit-id', '--name-only', '-r', '-z', 'HEAD', root=root)
        return [os.fsdecode(p) for p in data.split(b'\0') if p]
    data = git('diff', '--name-only', '-z', *revisions, root=root)
    return [os.fsdecode(p) for p in data.split(b'\0') if p]


def scoped_changes(paths: list[str] | None) -> dict[str, list[str] | None]:
    if paths is None:
        return dict.fromkeys(SCOPES)
    selected: dict[str, list[str] | None] = {}
    for scope in SCOPES:
        relevant = [p[len(scope) + 1:] for p in paths if p.startswith(scope + '/')]
        # Generated output and the installed skill are not source input.
        relevant = [p for p in relevant if not p.startswith(('graphify-out/', '.codex/'))]
        if relevant:
            selected[scope] = relevant
    return selected


def refresh(root: Path, changes: dict[str, list[str] | None]) -> None:
    from graphify.detect import classify_file, detect_incremental

    for scope, paths in changes.items():
        folder = root / scope
        output = folder / 'graphify-out'
        if not (output / 'graph.json').is_file():
            print(f'[scoped graphify] {scope}: no graph; run $graphify . in this folder', flush=True)
            continue
        semantic = paths is None or any(classify_file(folder / p) in ('document', 'paper', 'image', 'video') for p in paths)
        # A build committed alongside its source already covers that change.
        # Check semantic hashes before warning, rather than treating every
        # docs-only commit as stale even when its fresh graph is included.
        if semantic:
            incremental = detect_incremental(folder, manifest_path=str(output / 'manifest.json'), cache_root=folder)
            semantic = any(incremental.get('new_files', {}).get(kind) for kind in ('document', 'paper', 'image', 'video'))
            semantic = semantic or any(classify_file(folder / p) in ('document', 'paper', 'image', 'video')
                                       for p in incremental.get('deleted_files', []))
        # Keep the marker even if the AST update clears its own .needs_update flag.
        marker = output / '.needs_update'
        was_stale = marker.exists()
        try:
            print(f'[scoped graphify] updating {scope}', flush=True)
            subprocess.run([sys.executable, '-m', 'graphify', 'update', '.'], cwd=folder, check=True)
        finally:
            if semantic or was_stale:
                marker.write_text('Semantic files may have changed. Run $graphify . --update in this folder once at the end of the task.\n', encoding='utf-8')
                print(f'[scoped graphify] {scope}: semantic refresh required', flush=True)


def install(root: Path) -> None:
    # Honor the actual Git hooks directory (including core.hooksPath).
    hooks = Path(os.fsdecode(git('rev-parse', '--git-path', 'hooks', root=root)).strip())
    if not hooks.is_absolute():
        hooks = root / hooks
    hooks.mkdir(parents=True, exist_ok=True)
    python = Path(sys.executable).as_posix().replace("'", "'\"'\"'")
    for event in EVENTS:
        hook = hooks / event
        text = hook.read_text(encoding='utf-8') if hook.exists() else '#!/bin/sh\n'
        if START in text:
            first = text.index(START)
            last = text.index(END, first) + len(END)
            text = text[:first] + text[last:].lstrip('\r\n')
        block = f'''{START}
if [ -f tools/graphify/scoped_hooks.py ]; then
  _XERAHS_GRAPHIFY_PYTHON='{python}'
  if [ ! -x "$_XERAHS_GRAPHIFY_PYTHON" ]; then
    _XERAHS_GRAPHIFY_PYTHON=$(command -v python3 || command -v python)
  fi
  "$_XERAHS_GRAPHIFY_PYTHON" tools/graphify/scoped_hooks.py --background {event} "$@" || echo '[scoped graphify] dispatch failed; run foreground updates' >&2
fi
{END}
'''
        # Prepend after the shebang: upstream hooks may exit early on docs-only commits.
        first_line, separator, rest = text.partition('\n')
        if not first_line.startswith('#!'):
            rest = text
            first_line = '#!/bin/sh'
        hook.write_text(first_line + '\n' + block + rest, encoding='utf-8', newline='\n')
        hook.chmod(hook.stat().st_mode | 0o111)
        print(f'{event}: scoped dispatcher installed')


def background(root: Path, event: str, args: list[str]) -> None:
    gitdir = Path(os.fsdecode(git('rev-parse', '--absolute-git-dir', root=root)).strip())
    log = gitdir / 'graphify-scoped-hooks.log'
    command = [sys.executable, str(Path(__file__).resolve()), event, *args]
    options = {'creationflags': subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP} if os.name == 'nt' else {'start_new_session': True}
    with log.open('a', encoding='utf-8') as stream:
        subprocess.Popen(command, cwd=root, stdout=stream, stderr=subprocess.STDOUT,
                         stdin=subprocess.DEVNULL, close_fds=True, **options)
    print(f'[scoped graphify] background refresh log: {log}')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--background', action='store_true')
    parser.add_argument('--all', action='store_true', help='refresh every scoped graph in the foreground')
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('event', nargs='?', choices=(*EVENTS, 'install'), default='post-commit')
    parser.add_argument('hook_args', nargs='*')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    if args.event == 'install':
        install(root)
    elif args.background:
        background(root, args.event, args.hook_args)
    else:
        changes = scoped_changes(None if args.all else changed_files(root, args.event, args.hook_args))
        if args.dry_run:
            for scope in changes:
                print(scope)
        else:
            refresh(root, changes)


if __name__ == '__main__':
    main()
