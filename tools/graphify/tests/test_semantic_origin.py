import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

TOOL = Path(__file__).parents[1] / 'semantic_origin.py'
spec = importlib.util.spec_from_file_location('semantic_origin', TOOL)
origin = importlib.util.module_from_spec(spec)
spec.loader.exec_module(origin)


def fragment():
    return {
        'nodes': [{'id': 'docs_readme_topic', 'file_type': 'concept', 'source_location': 'L17'}],
        'edges': [{'source': 'docs_readme_topic', 'target': 'docs_readme_other',
                   'confidence': 'EXTRACTED', 'confidence_score': 1.0, 'source_location': 'L17'}],
        'hyperedges': [{'id': 'docs_readme_group', 'nodes': ['docs_readme_topic'], 'confidence': 'EXTRACTED'}],
        'coverage': {'docs/README.md': 23}, 'input_tokens': 100, 'output_tokens': 20,
    }


class SemanticOriginTests(unittest.TestCase):
    def test_line_locations_keep_semantic_provenance_without_changing_evidence(self):
        source = fragment()
        before = copy.deepcopy(source)
        prepared = origin.prepare_semantic_fragment(source)
        self.assertEqual(source, before)
        for kind in ('nodes', 'edges', 'hyperedges'):
            self.assertEqual(prepared[kind][0]['_origin'], 'semantic')
            unstamped = {key: value for key, value in prepared[kind][0].items() if key != '_origin'}
            self.assertEqual(unstamped, before[kind][0])
        self.assertEqual(prepared['coverage'], source['coverage'])
        prepared['coverage']['docs/README.md'] = 99
        prepared['hyperedges'][0]['nodes'].append('another')
        self.assertEqual(source, before)

    def test_explicit_ast_in_any_record_is_refused(self):
        for kind in ('nodes', 'edges', 'hyperedges'):
            with self.subTest(kind=kind):
                source = fragment()
                source[kind][0]['_origin'] = 'ast'
                with self.assertRaisesRegex(ValueError, 'explicit.*ast'):
                    origin.prepare_semantic_fragment(source)

    def test_existing_output_survives_invalid_or_ast_input(self):
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory) / 'input.json', Path(directory) / 'output.json'
            output.write_bytes(b'existing extraction\n')
            for invalid in ('invalid JSON', json.dumps({'nodes': [], 'edges': [{'_origin': 'ast'}]})):
                source.write_text(invalid)
                result = subprocess.run([sys.executable, str(TOOL), str(source), str(output)], capture_output=True)
                self.assertEqual(result.returncode, 2)
                self.assertEqual(output.read_bytes(), b'existing extraction\n')
                self.assertEqual(sorted(p.name for p in Path(directory).iterdir()), ['input.json', 'output.json'])

    def test_in_place_cli_preparation_is_idempotent(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'semantic.json'
            path.write_text(json.dumps(fragment()))
            for _ in range(2):
                result = subprocess.run([sys.executable, str(TOOL), str(path), str(path)], capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(path.read_text()), origin.prepare_semantic_fragment(fragment()))


if __name__ == '__main__':
    unittest.main()
