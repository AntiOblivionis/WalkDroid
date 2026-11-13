const fs = require('node:fs');
const path = require('node:path');
const sharp = require(process.env.WALKDROID_SHARP || 'sharp');

async function main() {
  const base = __dirname;
  const source = fs.readFileSync(path.join(base, 'walkdroid-icon.svg'), 'utf8');
  const transparent = source
    .replace(' on a dark blue rounded square', '')
    .replace(/  <rect id="background"[^>]+\/>\r?\n/, '');
  fs.writeFileSync(path.join(base, 'walkdroid-transparent.svg'), transparent);
  for (const [name, svg] of [['walkdroid-icon', source], ['walkdroid-transparent', transparent]]) {
    for (const size of [1024, 512, 256, 64]) {
      await sharp(Buffer.from(svg)).resize(size, size).png().toFile(path.join(base, `${name}-${size}.png`));
    }
  }
  const preview = Buffer.from(`<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="700" viewBox="0 0 1200 700">
    <rect width="1200" height="700" fill="#F4F3EA"/>
    <text x="670" y="137" font-family="Arial, sans-serif" font-size="50" font-weight="700" fill="#142F3A">WalkDroid</text>
    <text x="672" y="176" font-family="Arial, sans-serif" font-size="17" letter-spacing="3" fill="#4D6A6E">TAKE YOUR MUSIC FOR A WALK.</text>
    <text x="672" y="424" font-family="Arial, sans-serif" font-size="14" letter-spacing="2" fill="#4D6A6E">ICON / 128 / 64 / 32</text>
    <circle cx="687" cy="552" r="15" fill="#142F3A"/><circle cx="731" cy="552" r="15" fill="#70E2B1"/>
    <circle cx="775" cy="552" r="15" fill="#39B98D"/><circle cx="819" cy="552" r="15" fill="#FFAC69"/>
    <circle cx="863" cy="552" r="15" fill="#FFF4DF" stroke="#D9DFD6"/>
  </svg>`);
  const large = await sharp(Buffer.from(source)).resize(512,512).png().toBuffer();
  const cutout = await sharp(Buffer.from(transparent)).resize(218,218).png().toBuffer();
  const small = await Promise.all([128,64,32].map(size=>sharp(Buffer.from(source)).resize(size,size).png().toBuffer()));
  await sharp(preview).composite([
    {input:large,left:70,top:94},
    {input:cutout,left:663,top:183},
    {input:small[0],left:925,top:235},
    {input:small[1],left:1075,top:299},
    {input:small[2],left:1150,top:331}
  ]).png().toFile(path.join(base,'walkdroid-preview.png'));
  console.log('Exported SVG variants, PNGs at 1024/512/256/64, and preview.');
}
main().catch(error => { console.error(error); process.exitCode=1; });
