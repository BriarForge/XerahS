"""Apply the narrow code-refresh provenance fix to the active Graphify install."""
from __future__ import annotations
import os
from pathlib import Path
import tempfile

OLD = 'G = build_from_json(result, directed=bool((existing_graph_data or {}).get("directed", False)))'
NEW = 'G = build_from_json(result, directed=bool((existing_graph_data or {}).get("directed", False)), dedup=False)'

def apply() -> bool:
    import graphify.watch
    path = Path(graphify.watch.__file__)
    source = path.read_text(encoding='utf-8')
    if NEW in source:
        return False
    if source.count(OLD) != 1:
        raise RuntimeError('Unsupported Graphify code-refresh implementation; no file changed.')
    updated = source.replace(OLD, NEW)
    compile(updated, str(path), 'exec')
    descriptor, name = tempfile.mkstemp(prefix='.graphify-refresh-', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'w', encoding='utf-8') as stream:
            stream.write(updated)
        os.chmod(name, path.stat().st_mode)
        os.replace(name, path)
    finally:
        Path(name).unlink(missing_ok=True)
    return True

if __name__ == '__main__':
    print('Patched Graphify code refresh.' if apply() else 'Graphify code-refresh patch already applied.')
