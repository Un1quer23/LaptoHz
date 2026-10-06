// The large icon uses generated artwork; the tray has a simpler laptop drawing.
// Both are packed using only Node.js standard libraries.
const fs = require('node:fs');
const path = require('node:path');
const zlib = require('node:zlib');
const resources = path.join(__dirname, '..', 'resources');
const sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256];
const ink = [52, 54, 59], paper = [245, 243, 238];
const pngSignature = Buffer.from('89504e470d0a1a0a', 'hex');

function readPng(file) {
  const input = fs.readFileSync(file);
  if (!input.subarray(0, 8).equals(pngSignature)) throw new Error('Expected a PNG source.');
  let width, height;
  const compressed = [];
  for (let at = 8; at + 12 <= input.length;) {
    const length = input.readUInt32BE(at);
    if (at + length + 12 > input.length) throw new Error('Truncated PNG chunk.');
    const type = input.toString('ascii', at + 4, at + 8);
    const data = input.subarray(at + 8, at + 8 + length);
    if (type === 'IHDR') {
      width = data.readUInt32BE(0); height = data.readUInt32BE(4);
      if (data[8] !== 8 || data[9] !== 6 || data[10] || data[11] || data[12])
        throw new Error('Source must be a non-interlaced 8-bit RGBA PNG.');
    } else if (type === 'IDAT') compressed.push(data);
    else if (type === 'IEND') break;
    at += length + 12;
  }
  if (!width || !height || width > 4096 || height > 4096) throw new Error('Invalid PNG dimensions.');
  const stride = width * 4;
  const raw = zlib.inflateSync(Buffer.concat(compressed), { maxOutputLength: (stride + 1) * height });
  if (raw.length !== (stride + 1) * height) throw new Error('Invalid PNG image length.');
  const rgba = Buffer.alloc(stride * height);
  function paeth(a, b, c) {
    const p = a + b - c;
    const da = Math.abs(p - a), db = Math.abs(p - b), dc = Math.abs(p - c);
    return da <= db && da <= dc ? a : db <= dc ? b : c;
  }
  for (let y = 0; y < height; y++) {
    const filter = raw[y * (stride + 1)];
    if (filter > 4) throw new Error('Unsupported PNG row filter.');
    for (let x = 0; x < stride; x++) {
      const at = y * stride + x;
      const left = x >= 4 ? rgba[at - 4] : 0;
      const up = y ? rgba[at - stride] : 0;
      const upperLeft = y && x >= 4 ? rgba[at - stride - 4] : 0;
      const predictor = filter === 0 ? 0 : filter === 1 ? left : filter === 2 ? up :
        filter === 3 ? (left + up) >> 1 : paeth(left, up, upperLeft);
      rgba[at] = (raw[y * (stride + 1) + 1 + x] + predictor) & 255;
    }
  }
  return { width, height, rgba };
}

// Filter premultiplied alpha to avoid dark fringes against light taskbars.
function resize(source, size) {
  const rgba = Buffer.alloc(size * size * 4);
  const scaleX = source.width / size, scaleY = source.height / size;
  for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
    const x0 = x * scaleX, x1 = (x + 1) * scaleX;
    const y0 = y * scaleY, y1 = (y + 1) * scaleY;
    const color = [0, 0, 0]; let alpha = 0;
    for (let sy = Math.floor(y0); sy < Math.ceil(y1); sy++) {
      const wy = Math.min(y1, sy + 1) - Math.max(y0, sy);
      for (let sx = Math.floor(x0); sx < Math.ceil(x1); sx++) {
        const weight = wy * (Math.min(x1, sx + 1) - Math.max(x0, sx));
        const at = (sy * source.width + sx) * 4;
        const coverage = source.rgba[at + 3] * weight;
        alpha += coverage;
        for (let channel = 0; channel < 3; channel++) color[channel] += source.rgba[at + channel] * coverage;
      }
    }
    const at = (y * size + x) * 4;
    rgba[at + 3] = Math.round(alpha / (scaleX * scaleY));
    if (rgba[at + 3]) for (let channel = 0; channel < 3; channel++) rgba[at + channel] = Math.round(color[channel] / alpha);
  }
  return rgba;
}

function trayDrawing(size) {
  const rgba = Buffer.alloc(size * size * 4);
  // One optical stroke width for the bezel, base, lettering and open arrowheads.
  const stroke = Math.max(1, size * .018) / size;
  function rectangle(x, y, left, top, width, height) {
    return x >= left && x < left + width && y >= top && y < top + height;
  }
  function rounded(x, y, left, top, width, height, radius) {
    const dx = Math.max(left + radius - x, 0, x - (left + width - radius));
    const dy = Math.max(top + radius - y, 0, y - (top + height - radius));
    return rectangle(x, y, left, top, width, height) && dx * dx + dy * dy <= radius * radius;
  }
  function distance(x, y, ax, ay, bx, by) {
    const dx = bx - ax, dy = by - ay;
    const t = Math.max(0, Math.min(1, ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy)));
    return Math.hypot(x - ax - t * dx, y - ay - t * dy);
  }
  function line(x, y, ax, ay, bx, by) {
    return distance(x, y, ax, ay, bx, by) <= stroke / 2;
  }
  function arrow(x, y, centerY, left, right, head) {
    return line(x, y, left, centerY, right, centerY) ||
      line(x, y, left, centerY, left + head, centerY - head) ||
      line(x, y, left, centerY, left + head, centerY + head) ||
      line(x, y, right, centerY, right - head, centerY - head) ||
      line(x, y, right, centerY, right - head, centerY + head);
  }
  function hz(x, y) {
    const h = line(x, y, .355, .345, .355, .54) || line(x, y, .485, .345, .485, .54) ||
      line(x, y, .355, .438, .485, .438);
    const z = line(x, y, .535, .402, .645, .402) || line(x, y, .645, .402, .535, .54) ||
      line(x, y, .535, .54, .645, .54);
    return h || z;
  }
  const base = [[.06,.713],[.94,.713],[.935,.733],[.92,.758],[.90,.776],[.87,.785],
    [.13,.785],[.10,.776],[.08,.758],[.065,.733]];
  function baseAt(x, y) {
    let inside = false, edge = false;
    for (let i = 0, j = base.length - 1; i < base.length; j = i++) {
      const [ax, ay] = base[j], [bx, by] = base[i];
      if ((ay > y) !== (by > y) && x < (bx - ax) * (y - ay) / (by - ay) + ax) inside = !inside;
      edge ||= line(x, y, ax, ay, bx, by);
    }
    return edge ? ink : inside ? paper : null;
  }
  const samples = 8;
  for (let py = 0; py < size; py++) for (let px = 0; px < size; px++) {
    const color = [0, 0, 0]; let covered = 0;
    for (let sy = 0; sy < samples; sy++) for (let sx = 0; sx < samples; sx++) {
      const x = (px + (sx + .5) / samples) / size;
      const y = (py + (sy + .5) / samples) / size;
      const frame = rounded(x, y, .10, .21, .80, .51, .055);
      const screen = frame && rounded(x, y, .10 + stroke, .21 + stroke,
        .80 - 2 * stroke, .51 - 2 * stroke, Math.max(0, .055 - stroke));
      const mark = screen && (size <= 24 ? arrow(x, y, .465, .31, .69, .075) :
        hz(x, y) || arrow(x, y, .625, .355, .645, .047));
      const rgb = frame ? mark || !screen ? ink : paper : baseAt(x, y);
      if (!rgb) continue;
      for (let channel = 0; channel < 3; channel++) color[channel] += rgb[channel];
      covered++;
    }
    const at = (py * size + px) * 4;
    if (covered) for (let channel = 0; channel < 3; channel++) rgba[at + channel] = Math.round(color[channel] / covered);
    rgba[at + 3] = Math.round(255 * covered / (samples * samples));
  }
  return rgba;
}

function crc32(buffer) {
  let crc = 0xffffffff;
  for (const byte of buffer) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}

// Notification-area artwork has its own compact proportions and heavier,
// high-contrast strokes. The large application artwork is kept separately.
function notificationDrawing(size, dark) {
  const rgba = Buffer.alloc(size * size * 4);
  const foreground = dark ? paper : ink, background = dark ? ink : paper;
  const stroke = Math.max(1.5, Math.round(size * .08 * 2) / 2) / size;
  function rounded(x, y, left, top, width, height, radius) {
    const dx = Math.max(left + radius - x, 0, x - left - width + radius);
    const dy = Math.max(top + radius - y, 0, y - top - height + radius);
    return x >= left && x < left + width && y >= top && y < top + height && dx * dx + dy * dy <= radius * radius;
  }
  function line(x, y, ax, ay, bx, by) {
    const dx = bx - ax, dy = by - ay;
    const t = Math.max(0, Math.min(1, ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy)));
    return Math.hypot(x - ax - t * dx, y - ay - t * dy) <= stroke / 2;
  }
  const samples = 8;
  for (let py = 0; py < size; py++) for (let px = 0; px < size; px++) {
    const color = [0, 0, 0]; let covered = 0;
    for (let sy = 0; sy < samples; sy++) for (let sx = 0; sx < samples; sx++) {
      const x = (px + (sx + .5) / samples) / size;
      const y = (py + (sy + .5) / samples) / size;
      const frame = rounded(x, y, .075, .055, .85, .70, .085);
      const screen = frame && rounded(x, y, .075 + stroke, .055 + stroke,
        .85 - 2 * stroke, .70 - 2 * stroke, Math.max(0, .085 - stroke));
      const arrow = screen && (line(x, y, .255, .405, .745, .405) ||
        line(x, y, .255, .405, .365, .295) || line(x, y, .255, .405, .365, .515) ||
        line(x, y, .745, .405, .635, .295) || line(x, y, .745, .405, .635, .515));
      const base = y >= .755 && y < .935 && x >= .075 - (y - .755) * .28 && x < .925 + (y - .755) * .28;
      const rgb = frame ? !screen || arrow ? foreground : background : base ? foreground : null;
      if (!rgb) continue;
      for (let channel = 0; channel < 3; channel++) color[channel] += rgb[channel];
      covered++;
    }
    const at = (py * size + px) * 4;
    if (covered) for (let channel = 0; channel < 3; channel++) rgba[at + channel] = Math.round(color[channel] / covered);
    rgba[at + 3] = Math.round(255 * covered / (samples * samples));
  }
  return rgba;
}

function png(size, rgba) {
  function chunk(type, data) {
    const body = Buffer.concat([Buffer.from(type, 'ascii'), data]);
    const length = Buffer.alloc(4), crc = Buffer.alloc(4);
    length.writeUInt32BE(data.length); crc.writeUInt32BE(crc32(body));
    return Buffer.concat([length, body, crc]);
  }
  const header = Buffer.alloc(13);
  header.writeUInt32BE(size, 0); header.writeUInt32BE(size, 4); header[8] = 8; header[9] = 6;
  const raw = Buffer.alloc((size * 4 + 1) * size);
  for (let y = 0; y < size; y++) rgba.copy(raw, y * (size * 4 + 1) + 1, y * size * 4, (y + 1) * size * 4);
  return Buffer.concat([pngSignature, chunk('IHDR', header), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}

function bitmap(size, rgba) {
  const stride = Math.ceil(size / 32) * 4;
  const maskOffset = 40 + size * size * 4;
  const data = Buffer.alloc(maskOffset + stride * size);
  data.writeUInt32LE(40, 0); data.writeInt32LE(size, 4); data.writeInt32LE(size * 2, 8);
  data.writeUInt16LE(1, 12); data.writeUInt16LE(32, 14); data.writeUInt32LE(size * size * 4, 20);
  for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
    const source = (y * size + x) * 4;
    const at = 40 + ((size - y - 1) * size + x) * 4;
    data[at] = rgba[source + 2]; data[at + 1] = rgba[source + 1];
    data[at + 2] = rgba[source]; data[at + 3] = rgba[source + 3];
    if (!rgba[source + 3]) data[maskOffset + (size - y - 1) * stride + Math.floor(x / 8)] |= 0x80 >> (x % 8);
  }
  return data;
}

const source = readPng(path.join(resources, 'app-icon.png'));
const images = sizes.map(size => {
  const rgba = size <= 48 ? trayDrawing(size) : resize(source, size);
  return size <= 48 ? bitmap(size, rgba) : png(size, rgba);
});
function writeIcon(name, dimensions, frames) {
  const header = Buffer.alloc(6 + dimensions.length * 16);
  header.writeUInt16LE(1, 2); header.writeUInt16LE(dimensions.length, 4);
  let offset = header.length;
  frames.forEach((image, i) => {
    const at = 6 + i * 16; header[at] = dimensions[i] % 256; header[at + 1] = dimensions[i] % 256;
    header.writeUInt16LE(1, at + 4); header.writeUInt16LE(32, at + 6);
    header.writeUInt32LE(image.length, at + 8); header.writeUInt32LE(offset, at + 12); offset += image.length;
  });
  fs.writeFileSync(path.join(resources, name), Buffer.concat([header, ...frames]));
}
writeIcon('app.ico', sizes, images);
const traySizes = [16, 20, 24, 32, 40, 48, 64];
for (const dark of [false, true]) {
  const frames = traySizes.map(size => {
    const rgba = notificationDrawing(size, dark);
    return size <= 48 ? bitmap(size, rgba) : png(size, rgba);
  });
  writeIcon(dark ? 'tray-dark.ico' : 'tray-light.ico', traySizes, frames);
}
console.log(`Created resources/app.ico (${sizes.join(', ')} px).`);
console.log(`Created dedicated tray icons (${traySizes.join(', ')} px).`);
