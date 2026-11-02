"""Mark semantic-only extraction records before Graphify infers their tier."""
from __future__ import annotations

import argparse
import copy
import json
import os
from pathlib import Path
import tempfile


def prepare_semantic_fragment(fragment: dict) -> dict:
    """Return an owned semantic fragment; explicit AST input is a caller error."""
    if not isinstance(fragment, dict):
        raise ValueError('semantic fragment must be an object')
    prepared = copy.deepcopy(fragment)
    for key in ('nodes', 'edges', 'hyperedges'):
        if key not in prepared:
            if key == 'hyperedges':
                continue
            raise ValueError(f'semantic fragment is missing {key}')
        records = prepared[key]
        if not isinstance(records, list):
            raise ValueError(f'{key} must be an array')
        for index, record in enumerate(records):
            if not isinstance(record, dict):
                raise ValueError(f'{key}[{index}] must be an object')
            if record.get('_origin') not in (None, 'semantic'):
                raise ValueError(f'{key}[{index}] has explicit {record["_origin"]!r} origin; expected semantic-only input')
            record['_origin'] = 'semantic'
    return prepared


def prepare_file(source: Path, destination: Path) -> None:
    prepared = prepare_semantic_fragment(json.loads(source.read_text(encoding='utf-8')))
    encoded = json.dumps(prepared, ensure_ascii=False, indent=2) + '\n'
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=destination.parent,
                                         prefix=f'.{destination.name}.', delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, destination)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='semantic-only extraction JSON')
    parser.add_argument('destination', type=Path, help='prepared JSON; may equal source')
    args = parser.parse_args()
    try:
        prepare_file(args.source, args.destination)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
