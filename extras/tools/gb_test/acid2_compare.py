# SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
#
# SPDX-License-Identifier: MIT

"""Compare a gb_test frame dump against dmg-acid2's reference image.

    python3 acid2_compare.py out/dmg-acid2_f60.ppm roms/reference-dmg.png

Both images must use only the four DMG grey levels (255, 170, 85, 0, the
levels gb_test writes and the reference uses), and are compared shade by
shade. Mapping fixed levels, rather than ranking whatever levels each image
happens to contain, means two images that each use a different three of the
four shades can't be mistaken for a match. Prints PASS or the count of
wrong pixels, and on a mismatch writes <frame>_diff.png with the wrong
pixels in red.
"""
import sys
from PIL import Image


SHADE = {255: 0, 170: 1, 85: 2, 0: 3}


def shades(img, path):
    """Map every pixel to a shade index 0 (lightest) .. 3 (darkest)."""
    grey = img.convert("L")
    data = grey.tobytes()
    stray = sorted(set(data) - SHADE.keys())
    if stray:
        sys.exit(f"{path}: grey levels {stray} are not DMG shades")
    return grey, [SHADE[v] for v in data]


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    frame = Image.open(sys.argv[1])
    ref = Image.open(sys.argv[2])
    if frame.size != ref.size:
        sys.exit(f"size mismatch: {frame.size} vs reference {ref.size}")
    grey, got = shades(frame, sys.argv[1])
    _, want = shades(ref, sys.argv[2])
    wrong = [i for i, (g, w) in enumerate(zip(got, want)) if g != w]
    if not wrong:
        print(f"dmg-acid2: PASS ({frame.size[0]}x{frame.size[1]}, every pixel matches)")
        return 0
    w = frame.size[0]
    diff = grey.convert("RGB")
    for i in wrong:
        diff.putpixel((i % w, i // w), (255, 0, 0))
    out = sys.argv[1].rsplit(".", 1)[0] + "_diff.png"
    diff.save(out)
    print(f"dmg-acid2: FAIL, {len(wrong)} of {len(got)} pixels wrong -> {out}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
