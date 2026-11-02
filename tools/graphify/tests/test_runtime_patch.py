import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


@unittest.skipUnless(importlib.util.find_spec('graphify'), 'Use Graphify\'s Python interpreter')
class CodeRefreshTests(unittest.TestCase):
    def test_code_refresh_preserves_same_named_semantic_records(self):
        import networkx as nx
        from graphify.build import _load_existing_graph
        from graphify.export import to_json
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'main.py').write_text('def main():\n    return 1\n')
            (root / 'README.md').write_text('# Decision\nFirst decision.\nSecond decision.\n')
            output = root / 'graphify-out'
            output.mkdir()
            graph = nx.Graph()
            for index in (1, 2):
                graph.add_node(f'readme_decision_{index}', label='Decision', file_type='concept',
                               source_file='README.md', source_location=f'L{index+1}',
                               rationale=f'Distinct decision {index}', _origin='semantic')
            graph.add_edge('readme_decision_1', 'readme_decision_2', relation='references',
                           source_file='README.md', confidence='EXTRACTED', confidence_score=1.0, _origin='semantic')
            self.assertTrue(to_json(graph, {0: list(graph)}, str(output / 'graph.json')))
            for _ in range(2):
                result = subprocess.run([sys.executable, '-m', 'graphify', 'update', '.'], cwd=root,
                                        capture_output=True, text=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                nodes, _, _, _ = _load_existing_graph(output / 'graph.json')
                saved = {node['id']: node for node in nodes}
                for index in (1, 2):
                    self.assertEqual(saved[f'readme_decision_{index}']['rationale'], f'Distinct decision {index}')
                    self.assertEqual(saved[f'readme_decision_{index}']['_origin'], 'semantic')


if __name__ == '__main__':
    unittest.main()
