import xml.etree.ElementTree as ET
import subprocess
from PIL import Image

namespace = {
    'svg': 'http://www.w3.org/2000/svg'
}


class Svg:
    def __init__(self, svg_file, width, height):
        self.tree = ET.parse(svg_file)
        self.root = self.tree.getroot()
        self.width = width
        self.height = height

    def frame_count(self):
        return len(self.root.findall('.//svg:g[@type="frame"]', namespace))

    def rasterize(self, frame):
        tmp_svg = f'tmp{frame}.svg'
        tmp_png = f'tmp{frame}.png'

        for e in self.root.findall(f'.//svg:g[@type="frame"]', namespace):
            e.set('style', 'display:none')
        for e in self.root.findall(f'.//svg:g[@type="frame"][@frame="{frame}"]', namespace):
            e.set('style', 'display:inline')
        self.tree.write(tmp_svg)
        subprocess.run([
            'inkscape',
            '--export-type=png',
            f'--export-width={self.width}',
            f'--export-height={self.height}',
            tmp_svg])

        return Image.open(tmp_png)

    def cleanup(self, frame):
        subprocess.run(['rm', f'tmp{frame}.svg', f'tmp{frame}.png'])


def make_bin(svg_file, name, width, height):
    svg = Svg(svg_file, width, height)

    bytes = []

    num_frames = svg.frame_count()
    bytes.append(num_frames)

    images = []
    for n in range(1, num_frames + 1):
        im = svg.rasterize(n)

        bytes.append(im.width)
        bytes.append(im.height)

        for y in range(im.height):
            for x in range(im.width):
                r, g, b, a = im.getpixel((x, y))
                bytes.extend([r, g, b])

        images.append(im)

    images[0].save(f'{name}.gif', save_all=True, append_images=images[1:], optimize=False, duration=70, loop=0)

    for n in range(1, num_frames + 1):
        svg.cleanup(n)

    with open(f'{name}.bin', 'wb') as f:
        f.write(bytearray(bytes))


def main():
    make_bin('wave.svg', 'wave', 32, 16)
    make_bin('fadein.svg', 'fadein', 32, 16)
    make_bin('fadeout.svg', 'fadeout', 32, 16)
    make_bin('arrows.svg', 'arrows', 7, 10)


if __name__ == '__main__':
    main()
