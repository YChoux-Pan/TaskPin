"""Prepare Windows icon sizes from the preserved Canva PNG (requires Pillow)."""
import argparse
from collections import Counter
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('input', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--preview', type=Path)
args = parser.parse_args()
image = Image.open(args.input).convert('RGBA')
if min(image.size) < 512 or image.width != image.height:
    raise SystemExit('Expected the square Canva export at its original resolution.')

# Remove only the connected exterior. White note/pin shapes inside the tile stay opaque.
ImageDraw.floodfill(image, (0, 0), (0, 0, 0, 0), thresh=45)
alpha = image.getchannel('A')
edge = ImageChops.multiply(alpha, ImageChops.invert(alpha).filter(ImageFilter.MaxFilter(9)))
colors = Counter(image.convert('RGB').get_flattened_data())
source_blue = max((c for c in colors if c[2] > c[0] + 60 and c[2] > c[1] + 50), key=colors.get)
source_white = max((c for c in colors if min(c) > 230), key=colors.get)
delta = tuple(w - b for b, w in zip(source_blue, source_white))
denominator = sum(d * d for d in delta)
brand_blue, white = (76, 154, 255), (255, 255, 255)
pixels = []
for rgba, boundary in zip(image.get_flattened_data(), edge.get_flattened_data()):
    if not rgba[3]:
        pixels.append((0, 0, 0, 0))
        continue
    weight = max(0.0, min(1.0, sum((v - b) * d for v, b, d in zip(rgba[:3], source_blue, delta)) / denominator))
    weight = 0.0 if weight < 0.08 else 1.0 if weight > 0.92 else weight
    if boundary:
        pixels.append((*brand_blue, round(255 * (1.0 - weight))))
    else:
        rgb = tuple(round(b * (1.0 - weight) + w * weight) for b, w in zip(brand_blue, white))
        pixels.append((*rgb, 255))
image.putdata(pixels)
bounds = image.getbbox()
if not bounds:
    raise SystemExit('No icon content found.')
tile = image.crop(bounds)
tile.thumbnail((928, 928), Image.Resampling.LANCZOS)
master = Image.new('RGBA', (1024, 1024))
master.alpha_composite(tile, ((1024 - tile.width) // 2, (1024 - tile.height) // 2))
args.output.mkdir(parents=True, exist_ok=True)
master.save(args.output / 'taskpin.png', optimize=True)
sizes = (16, 20, 24, 32, 40, 48, 64, 128, 256)
for size in sizes:
    master.resize((size, size), Image.Resampling.LANCZOS).save(args.output / f'taskpin-{size}.png', optimize=True)
master.save(args.output / 'taskpin.ico', sizes=[(s, s) for s in sizes])
if args.preview:
    sheet = Image.new('RGB', (960, 540), '#ECEEF3')
    draw = ImageDraw.Draw(sheet)
    draw.rectangle((480, 0, 959, 539), fill='#12171F')
    for left in (0, 480):
        sheet.paste(master.resize((256, 256), Image.Resampling.LANCZOS), (left + 112, 40),
                    master.resize((256, 256), Image.Resampling.LANCZOS))
        for x, size in zip((32, 126, 220, 314, 408), (16, 24, 32, 48, 64)):
            small = master.resize((size, size), Image.Resampling.LANCZOS)
            sheet.paste(small, (left + x - size // 2, 373 - size // 2), small)
            draw.text((left + x - 8, 430), str(size), fill='#2E333B' if left == 0 else '#E8ECF2')
    args.preview.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(args.preview)
print(f'Prepared transparent PNG, {len(sizes)} sizes and ICO; source tile bounds={bounds}, source blue={source_blue}')
