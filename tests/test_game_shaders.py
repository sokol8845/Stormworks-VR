"""Read-only checks of the installed game's skinned shaders against AvatarPose assumptions.

GlAvatarPose discovers palettes by uniform name and authorises single-palette
draws only for depth-only shaders. A game update that renames uniforms, adds a
previous palette to depth shaders or changes the clip selector would silently
disable or misapply the avatar pose; these checks fail first instead.
Nothing is executed or modified; the test is skipped without an installation.
"""
import os
from pathlib import Path
import re
import unittest

GAME = Path(os.environ.get('SWVR_GAME_DIR', r'C:\Program Files (x86)\Steam\steamapps\common\Stormworks'))
SHADERS = GAME/'rom'/'graphics'/'shaders'
COLOUR = ('opaque_anim', 'glass_anim', 'additive_anim')
DEPTH = ('depth_anim', 'depth_anim_cheap')


def uniforms(name):
    text = (SHADERS/f'{name}.glslv').read_text(encoding='utf-8', errors='replace')
    found = {}
    text = re.sub(r'//[^\n]*|/\*.*?\*/', '', text, flags=re.S)
    for kind, names in re.findall(r'\buniform\s+(\w+)\s+([^;]+);', text):
        for item in names.split(','):
            match = re.fullmatch(r'\s*(\w+)\s*(?:\[(\d+)\])?\s*', item)
            if match:
                found[match[1]] = (kind, int(match[2]) if match[2] else None)
    return found


@unittest.skipUnless(SHADERS.is_dir(), 'Stormworks shaders are not installed')
class GameShaderAssumptions(unittest.TestCase):
    def test_skinned_shader_set_is_known(self):
        skinned = {p.stem for p in SHADERS.glob('*.glslv')
                   if re.search(r'\bbone_matrices\b', p.read_text(encoding='utf-8', errors='replace'))}
        self.assertEqual(skinned, set(COLOUR+DEPTH), 'new or removed skinned shader needs AvatarPose review')

    def test_colour_shaders_pair_palettes_with_clip_selector(self):
        for name in COLOUR:
            with self.subTest(name=name):
                u = uniforms(name)
                self.assertEqual(u.get('bone_matrices'), ('mat4', 64))
                self.assertEqual(u.get('bone_matrices_prev'), ('mat4', 64))
                self.assertEqual(u.get('mat_world'), ('mat4', None))
                self.assertEqual(u.get('bone_clip_index'), ('int', None))
                self.assertEqual(u.get('bone_clip_weight'), ('float', None))

    def test_depth_shaders_have_single_palette_only(self):
        # Single-palette provenance is safe only while depth shaders have no
        # previous palette and no clip selector (avatar_pose_gl.cpp before_draw).
        for name in DEPTH:
            with self.subTest(name=name):
                u = uniforms(name)
                self.assertEqual(u.get('bone_matrices'), ('mat4', 64))
                self.assertEqual(u.get('mat_world'), ('mat4', None))
                for absent in ('bone_matrices_prev', 'bone_clip_index', 'bone_clip_weight'):
                    self.assertNotIn(absent, u)


if __name__ == '__main__':
    unittest.main()
