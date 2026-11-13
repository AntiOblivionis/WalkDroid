"""Build outlined branding and an Android 9 bootanimation. Local files only."""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import zipfile

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw

SOURCE = Path(__file__).resolve().parent
ROOT = SOURCE
BOOT = ROOT / 'bootanimation'
BG = '#142F3A'
MINT = '#70E2B1'
CREAM = '#FFF4DF'
ORANGE = '#FFAC69'
FPS = 24
FRAMES = 48
CROP = (72, 200, 408, 568)
FONT = ROOT / 'fonts/JetBrainsMono-SemiBold.ttf'
font = glyphs = cmap = None
units = advance = 0


def outlined_text(text, x, baseline, size, color):
    """Use the actual JetBrains Mono outlines, not a font-name fallback."""
    scale = size / units
    paths = []
    for char in text:
        name = cmap[ord(char)]
        pen = SVGPathPen(glyphs)
        glyphs[name].draw(pen)
        if pen.getCommands():
            paths.append(f'<path d="{pen.getCommands()}" transform="translate({x:.4f} {baseline}) scale({scale:.6f} {-scale:.6f})" fill="{color}"/>')
        x += font['hmtx'][name][0] * scale
    return ''.join(paths)


def wordmark(width, baseline, size, letters=9, cursor_opacity=1):
    step = advance * size / units
    x = (width - 12 * step) / 2
    prompt = outlined_text('>', x, baseline, size, MINT)
    name = outlined_text('WalkDroid'[:letters], x + 2 * step, baseline, size, CREAM)
    cursor_x = x + (2 + letters) * step + step * .08
    cursor = f'<rect id="terminal-cursor" x="{cursor_x:.3f}" y="{baseline+5}" width="{step*.83:.3f}" height="{max(3,size*.09):.3f}" rx="1" fill="{ORANGE}" opacity="{cursor_opacity:.3f}"/>'
    return f'<g id="wordmark" aria-label="WalkDroid">{prompt}{name}{cursor}</g>'


def mascot(source):
    transparent = re.sub(r'  <rect id="background"[^>]+/>\s*', '', source)
    match = re.search(r'<g id="walkdroid".*</g>\s*</svg>', transparent, re.S)
    if match:
        return re.sub(r'\s*</svg>\s*$', '', match.group(0)).strip()
    # Other SVG sources can use their complete inner content as the artwork.
    return re.sub(r'</svg>\s*$', '', re.sub(r'^\s*(?:<\?xml[^>]*>\s*)?<svg[^>]*>', '', transparent, count=1), count=1).strip()


def make_about(art):
    logo = f'<g transform="translate(64 -18)">{art}</g>'
    label = wordmark(640, 534, 46)
    for transparent in [False, True]:
        bg = '' if transparent else f'<rect width="640" height="640" rx="56" fill="{BG}"/>'
        name = 'walkdroid-about-transparent' if transparent else 'walkdroid-about'
        svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="1280" height="1280" viewBox="0 0 640 640" fill="none" role="img" aria-labelledby="title desc">
<title id="title">WalkDroid — about-page logo</title>
<desc id="desc">WalkDroid mascot above a terminal prompt. The WalkDroid name uses JetBrains Mono SemiBold v2.304 converted to vector outlines.</desc>
{bg}{logo}{label}</svg>'''
        (ROOT / f'{name}.svg').write_text(svg, encoding='utf-8')


def animated_mascot(art, phase):
    # All motion is periodic over 48 frames; static pose and palette are retained.
    p = 2 * math.pi * phase
    bars = [6+6*(1-math.cos(2*p)), 17+5*math.sin(p), 22-7*(1-math.cos(p)), 13+8*math.sin(2*p)]
    equalizer = ''.join(f'<path d="M{366+i*8} {215-h:.3f}V215"/>' for i,h in enumerate(bars))
    result = re.sub(r'(<g id="equalizer"[^>]*>).*?(</g>)', lambda m:m[1]+equalizer+m[2], art, flags=re.S)
    pulse = .60 + .40*(.5+.5*math.cos(2*p))
    result = result.replace('id="motion"', f'id="motion" opacity="{pulse:.4f}"')
    bob = -2.2*(1-math.cos(2*p))
    tilt = .65 * math.sin(p)
    return f'<g transform="translate(0 {bob:.4f}) rotate({tilt:.4f} 256 256)">{result}</g>'


def make_scene(art, part, index):
    if part == 'part0':
        time = index/FPS
        ease = min(1, time/.65)
        ease = 1-(1-ease)**3
        fade, offset = ease, 16*(1-ease)
        letters = max(0, min(9, int((time-.70)*12)))
        label_opacity = max(0, min(1, (time-.55)/.18))
        cursor = 1
        logo = art
    else:
        phase = index/FRAMES
        fade, offset, letters, label_opacity = 1, 0, 9, 1
        cursor = .2+.8*(.5+.5*math.cos(4*math.pi*phase))
        logo = animated_mascot(art, phase)
    # Coordinates are in the full 480x800 screen; viewBox is the trimmed texture.
    label = wordmark(480, 530, 38, letters, cursor)
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="336" height="368" viewBox="72 200 336 368" fill="none">
<rect x="72" y="200" width="336" height="368" fill="{BG}"/>
<g opacity="{fade:.5f}" transform="translate(60 {160+offset:.5f}) scale(.703125)">{logo}</g>
<g opacity="{label_opacity:.5f}">{label}</g></svg>'''


def make_sources(art):
    for part in ['part0','part1']:
        folder = BOOT / 'sources' / part
        folder.mkdir(parents=True,exist_ok=True)
        for index in range(FRAMES):
            (folder / f'{index:04d}.svg').write_text(make_scene(art,part,index),encoding='utf-8')


def full_frame(image):
    result = Image.new('RGB',(480,800),BG)
    result.paste(image,(CROP[0],CROP[1]))
    return result


def package():
    desc = f'480 800 {FPS}\np 1 0 part0 {BG}\np 0 0 part1 {BG}\n'
    trim = '336x368+72+200\n' * FRAMES
    (BOOT / 'desc.txt').write_text(desc,encoding='ascii',newline='\n')
    out = BOOT / 'bootanimation.zip'
    # Stable timestamps and stored entries match the Android 9 format.
    def write_entry(archive, name, data):
        item = zipfile.ZipInfo(name,(2026,10,23,0,0,0))
        item.compress_type = zipfile.ZIP_STORED
        item.external_attr = 0o100644 << 16
        archive.writestr(item,data)
    with zipfile.ZipFile(out,'w') as archive:
        write_entry(archive,'desc.txt',desc.encode('ascii'))
        for part in ['part0','part1']:
            for index in range(FRAMES):
                file = BOOT / 'frames' / part / f'{index:04d}.png'
                write_entry(archive,f'{part}/{file.name}',file.read_bytes())
            write_entry(archive,f'{part}/trim.txt',trim.encode('ascii'))
    return out


def previews():
    # Preview includes the one-shot intro followed by three complete loops.
    originals = {}
    for part in ['part0','part1']:
        originals[part] = [full_frame(Image.open(BOOT/'frames'/part/f'{i:04d}.png').convert('RGB')) for i in range(FRAMES)]
    selected = originals['part0'][::2] + originals['part1'][::2]*3
    # One global palette prevents flicker in the GIF. Android keeps RGB PNG frames.
    palette_src = Image.new('RGB',(480*4,800),BG)
    for i,img in enumerate([originals['part0'][24],*originals['part1'][::16]]):
        palette_src.paste(img,(480*i,0))
    palette = palette_src.quantize(colors=240)
    gif_frames = [img.quantize(palette=palette,dither=Image.Dither.NONE) for img in selected]
    durations = [80,80,90]*int(len(gif_frames)/3)
    gif_frames[0].save(BOOT/'walkdroid-boot-preview.gif',save_all=True,append_images=gif_frames[1:],duration=durations,loop=0,disposal=1,optimize=False)
    small_frames = [img.resize((240,400),Image.Resampling.LANCZOS).quantize(palette=palette,dither=Image.Dither.NONE) for img in selected]
    small_frames[0].save(BOOT/'walkdroid-boot-preview-240.gif',save_all=True,append_images=small_frames[1:],duration=durations,loop=0,disposal=1,optimize=False)
    originals['part1'][0].save(BOOT/'walkdroid-boot-poster.png')
    # A full-color APNG retains the actual 24 fps cadence for close inspection.
    frames = originals['part0'] + originals['part1']*2
    frames[0].save(BOOT/'walkdroid-boot-preview.png',save_all=True,append_images=frames[1:],duration=1000/FPS,loop=0)
    sheet = Image.new('RGB',(240*5,400+32),'#EAEFEA')
    draw = ImageDraw.Draw(sheet)
    for col,(part,index,label) in enumerate([('part0',8,'0.33s / fade'),('part0',24,'1.00s / type'),('part0',47,'1.96s / ready'),('part1',12,'loop / A'),('part1',36,'loop / B')]):
        sheet.paste(originals[part][index].resize((240,400),Image.Resampling.LANCZOS),(col*240,0))
        draw.text((col*240+10,410),label,fill=BG)
    sheet.save(BOOT/'walkdroid-boot-contact-sheet.png')


def main():
    global ROOT, BOOT, FONT, font, glyphs, cmap, units, advance
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--node',default=shutil.which('node'))
    parser.add_argument('--icon',type=Path,default=SOURCE/'walkdroid-icon.svg',help='source SVG')
    parser.add_argument('--font',type=Path,default=SOURCE/'fonts/JetBrainsMono-SemiBold.ttf',help='wordmark font')
    parser.add_argument('--output',type=Path,default=SOURCE/'build-output',help='directory for generated assets')
    parser.add_argument('--no-previews',action='store_true',help='skip GIF and contact-sheet previews')
    args=parser.parse_args()
    if not args.node:
        parser.error('Pass --node or put Node.js on PATH')
    ROOT=args.output.resolve();BOOT=ROOT/'bootanimation';FONT=args.font.resolve()
    ROOT.mkdir(parents=True,exist_ok=True)
    for source,target in ((args.icon.resolve(),ROOT/'walkdroid-icon.svg'),
                          (SOURCE/'export.cjs',ROOT/'export.cjs'),
                          (SOURCE/'render-assets.cjs',ROOT/'render-assets.cjs')):
        if source.resolve()!=target.resolve():shutil.copyfile(source,target)
    font=TTFont(FONT);glyphs=font.getGlyphSet();cmap=font.getBestCmap()
    units=font['head'].unitsPerEm;advance=font['hmtx'][cmap[ord('W')]][0]
    subprocess.run([args.node,str(ROOT/'export.cjs')],check=True)
    art=mascot((ROOT/'walkdroid-icon.svg').read_text('utf-8'))
    make_about(art)
    make_sources(art)
    subprocess.run([args.node,str(ROOT/'render-assets.cjs')],check=True)
    output=package()
    if not args.no_previews:previews()
    record={'icon':str(args.icon.resolve()),'font':str(FONT),'node':str(args.node),
            'bootanimation':str(output.relative_to(ROOT)),'fps':FPS,
            'intro_frames':FRAMES,'loop_frames':FRAMES}
    (ROOT/'build.lock.json').write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('Generated assets:',ROOT)


if __name__=='__main__':
    main()
