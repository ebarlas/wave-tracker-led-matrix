import xml.etree.ElementTree as ET
import subprocess
from PIL import Image

namespace = {
    'svg': 'http://www.w3.org/2000/svg'
}


def rasterize(svg_file):
    tree = ET.parse(svg_file)
    root = tree.getroot()

    bytes = []

    num_frames = len(root.findall('.//svg:g[@type="frame"]', namespace))
    bytes.append(num_frames)

    images = []

    for n in range(1, num_frames + 1):
        tmp_svg = f'tmp{n}.svg'
        tmp_png = f'tmp{n}.png'

        for e in root.findall(f'.//svg:g[@type="frame"]', namespace):
            e.set('style', 'display:none')
        for e in root.findall(f'.//svg:g[@type="frame"][@frame="{n}"]', namespace):
            e.set('style', 'display:inline')
        tree.write(tmp_svg)
        subprocess.run([
            'inkscape',
            '--export-type=png',
            '--export-width=32',
            '--export-height=16',
            tmp_svg])

        im = Image.open(tmp_png)

        bytes.append(im.width)
        bytes.append(im.height)

        for y in range(im.height):
            for x in range(im.width):
                r, g, b, a = im.getpixel((x, y))
                bytes.extend([r, g, b])

        images.append(im)

        subprocess.run(['rm', tmp_svg, tmp_png])

    images[0].save('wave.gif', save_all=True, append_images=images[1:], optimize=False, duration=70, loop=0)

    with open('wave.bin', 'wb') as f:
        f.write(bytearray(bytes))


rasterize('wave.svg')
