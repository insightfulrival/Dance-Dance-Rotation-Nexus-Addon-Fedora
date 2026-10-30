import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('generate_data', ROOT / 'tools/generate_data.py')
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


def song(**kwargs):
    base = {'name': 'Example', 'notes': [], 'decodedBuildTemplate': {'profession': 6, 'specializations': [{'id': 31}, {'id': 56}]}}
    base.update(kwargs)
    return base


class GeneratorTests(unittest.TestCase):
    def test_translator_rules(self):
        decoded = generator.decode_song(song(notes=[
            {'time': 2.5, 'duration': 3.5, 'noteType': 'Utility', 'abilityId': '44364'},
            {'time': 1, 'duration': 0, 'noteType': 'Weapon1', 'abilityId': 5, 'overrideAuto': True}]))
        # Unknown note names become Unknown; numeric strings are accepted; halves round to even;
        # notes are stably sorted by time.
        self.assertEqual(decoded['notes'], [(1, 0, 3, 5, True), (2, 4, 18, 44364, False)])
        self.assertEqual(decoded['profession'], 7)  # Elementalist in the module's enum order
        self.assertEqual(decoded['specialization'], 56)
        self.assertEqual(decoded['utility'], [0, 0, 0])

    def test_reference_failures(self):
        for bad in [song(name=''), song(notes=None), {'name': 'x', 'notes': []},
                    song(decodedBuildTemplate={'specializations': []}),
                    song(notes=[{'time': 'soon'}])]:
            with self.assertRaises((ValueError, KeyError, TypeError)):
                generator.decode_song(bad)

    def test_bundled_data(self):
        text, songs, notes, abilities = generator.render_data(ROOT)
        raw = json.loads((ROOT / 'assets/data/defaultSongs.json').read_text())
        self.assertEqual(len(raw), 155)
        self.assertEqual(songs, len({entry['name'] for entry in raw}))  # Later duplicates replace earlier ones
        self.assertEqual((songs, notes, abilities), (102, 22194, 25887))
        self.assertIn('"Power Weaver [SC]"', text)

    def test_images_are_pngs(self):
        header = generator.render_images(ROOT)
        self.assertEqual(header.count('DDR_IMG_'), len(generator.IMAGES))


if __name__ == '__main__':
    unittest.main()
