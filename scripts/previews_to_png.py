#!/usr/bin/env python3
"""Convert build/previews/*.pbm (host-rendered UI screens) to PNG, plus a
contact sheet build/previews/_sheet.png for quick review."""
import glob
import os
from PIL import Image

files = sorted(glob.glob("build/previews/*.pbm"))
imgs = []
for f in files:
    im = Image.open(f).convert("L")
    im.save(f[:-4] + ".png")
    imgs.append((os.path.basename(f)[:-4], im))
    os.remove(f)
if imgs:
    pad = 20
    W = sum(im.width for _, im in imgs if im.width == 480) // max(1, 1)
    rows = [[im for _, im in imgs if im.width == 480], [im for _, im in imgs if im.width != 480]]
    sheet_w = max(sum(i.width + pad for i in r) for r in rows if r) + pad
    sheet_h = sum(max(i.height for i in r) + pad for r in rows if r) + pad
    sheet = Image.new("L", (sheet_w, sheet_h), 140)
    y = pad
    for r in rows:
        if not r:
            continue
        x = pad
        for i in r:
            sheet.paste(i, (x, y))
            x += i.width + pad
        y += max(i.height for i in r) + pad
    sheet.save("build/previews/_sheet.png")
print(f"converted {len(imgs)} previews")
