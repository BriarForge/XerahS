import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('scoped_hooks', Path(__file__).parents[1] / 'scoped_hooks.py')
hooks = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hooks)


class ScopedHookTests(unittest.TestCase):
    def test_routes_only_affected_source_scopes(self):
        self.assertEqual(hooks.scoped_changes([
            'platforms/linux/linux-qt/app/main.cpp',
            'product-contract/capabilities/EDITOR-CANVAS-001/SPEC.md',
            'platforms/windows/graphify-out/graph.json',
            'platforms/macos/.codex/skills/graphify/SKILL.md',
            'platforms/linuxish/README.md',
        ]), {
            'platforms/linux': ['linux-qt/app/main.cpp'],
            'product-contract': ['capabilities/EDITOR-CANVAS-001/SPEC.md'],
        })

    def test_git_paths_preserve_spaces_and_unicode(self):
        with patch.object(hooks, 'git', return_value='platforms/linux/naïve name.cpp\0'.encode()):
            self.assertEqual(hooks.changed_files(Path('.'), 'post-commit', []), ['platforms/linux/naïve name.cpp'])

    def test_checkout_uses_both_revisions_and_ignores_file_checkout(self):
        with patch.object(hooks, 'git', return_value=b'platforms/windows/README.md\0') as git:
            self.assertEqual(hooks.changed_files(Path('.'), 'post-checkout', ['old', 'new', '0']), [])
            git.assert_not_called()
            self.assertEqual(hooks.changed_files(Path('.'), 'post-checkout', ['old', 'new', '1']), ['platforms/windows/README.md'])
            git.assert_called_once_with('diff', '--name-only', '-z', 'old', 'new', root=Path('.'))

    def test_semantic_staleness_survives_ast_update(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            out = root / 'product-contract/graphify-out'
            out.mkdir(parents=True)
            (out / 'graph.json').write_text('{}')
            def update(*args, **kwargs):
                (out / '.needs_update').unlink(missing_ok=True)
            with patch.object(hooks.subprocess, 'run', side_effect=update), patch('graphify.detect.detect_incremental', return_value={'new_files': {'document': ['README.md']}}):
                hooks.refresh(root, {'product-contract': ['README.md']})
                self.assertTrue((out / '.needs_update').exists())
                hooks.refresh(root, {'product-contract': ['data.json']})
                self.assertTrue((out / '.needs_update').exists())

    def test_already_refreshed_document_commit_is_not_marked_stale(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            out = root / 'product-contract/graphify-out'
            out.mkdir(parents=True)
            (out / 'graph.json').write_text('{}')
            with patch.object(hooks.subprocess, 'run'), patch('graphify.detect.detect_incremental', return_value={'new_files': {}}):
                hooks.refresh(root, {'product-contract': ['README.md']})
            self.assertFalse((out / '.needs_update').exists())

    def test_installer_is_idempotent_and_keeps_existing_hook(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = root / '.git/hooks'
            folder.mkdir(parents=True)
            (folder / 'post-commit').write_text('#!/bin/sh\necho existing\nexit 0\n')
            with patch.object(hooks, 'git', return_value=b'.git/hooks\n'):
                hooks.install(root)
                hooks.install(root)
            script = (folder / 'post-commit').read_text()
            self.assertEqual(script.count(hooks.START), 1)
            self.assertIn('echo existing\nexit 0', script)
            self.assertLess(script.index(hooks.START), script.index('exit 0'))


if __name__ == '__main__':
    unittest.main()
