// Rasterize vector sources. No installed system font is required.
const fs = require('node:fs');
const path = require('node:path');
const sharp = require(process.env.WALKDROID_SHARP || 'sharp');
sharp.concurrency(2);
async function main() {
  for (const name of ['walkdroid-about', 'walkdroid-about-transparent']) {
    const source = path.join(__dirname, `${name}.svg`);
    await sharp(source).png().toFile(path.join(__dirname, `${name}.png`));
    await sharp(source).resize(480).png().toFile(path.join(__dirname, `${name}-480.png`));
  }
  const root = path.join(__dirname, 'bootanimation');
  for (const part of ['part0', 'part1']) {
    const sourceDir = path.join(root, 'sources', part);
    const outputDir = path.join(root, 'frames', part);
    fs.mkdirSync(outputDir, { recursive: true });
    const files = fs.readdirSync(sourceDir).filter(name=>name.endsWith('.svg')).sort();
    for (const file of files) {
      // Render at 2x and downsample for clean edges on the 480x800 display.
      await sharp(path.join(sourceDir,file), {density:144})
        .resize(336,368)
        .removeAlpha()
        .png({compressionLevel:9})
        .toFile(path.join(outputDir,file.replace('.svg','.png')));
    }
    console.log(`Rendered ${part}: ${files.length} frames`);
  }
}
main().catch(error=>{console.error(error);process.exitCode=1;});
