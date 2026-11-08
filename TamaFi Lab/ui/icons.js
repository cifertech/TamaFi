(() => {
  // Same 16x16 2-bit rows as firmware ui_icons.cpp, in stamp order.
  const BITS = [
    [0x00000000, 0x00000000, 0x00FFFF00, 0x0FC003F0, 0x3C00003C, 0xF03FFC0F, 0x03F00FC0, 0x0F0000F0,
     0x0003C000, 0x003FFC00, 0x00F00F00, 0x00000000, 0x0003C000, 0x0003C000, 0x00000000, 0x00000000],
    [0x00000000, 0x01BE4000, 0x06419000, 0x18002400, 0x24001800, 0x30000C00, 0x30000C00, 0x24000800,
     0x18002400, 0x0640B400, 0x01BE5F80, 0x00000FE0, 0x00000BF8, 0x000002FE, 0x000000BE, 0x00000028],
    [0x00000000, 0x00000000, 0x006BA400, 0x06901A40, 0x191B9190, 0x24766460, 0x909DD818, 0xD0E76C1C,
     0x509DD814, 0x24667460, 0x191B9190, 0x06901A40, 0x006BA400, 0x00000000, 0x00000000, 0x00000000],
    [0x00180002, 0x00000000, 0x41AAA050, 0x42000800, 0x02500800, 0x0200A804, 0x0201500B, 0x02020088,
     0x082A00A4, 0xC86A4004, 0x90528004, 0x807E0FFF, 0x10560000, 0x0AB70000, 0x02000000, 0x02400000],
    [0x00FFFF00, 0x0FFFFFF0, 0x3FFFFFFC, 0x3FFFFFFC, 0xFF0FF0FF, 0xFC03C03F, 0xFC03C03F, 0xFF0FF0FF,
     0x3FFFFFFC, 0x0FFFFFF0, 0x0FFFFFF0, 0x0FFFFFF0, 0x00F3CF00, 0x00F3CF00, 0x00F3CF00, 0x00000000],
    [0x00000000, 0xC0C0000F, 0x3300000F, 0x0C00000F, 0x3300000F, 0xC0C03C0F, 0x00003C0F, 0x00003C0F,
     0x003C3C0F, 0x003C3C0F, 0x003C3C0F, 0x003C3C0F, 0xF03C3C0F, 0xF03C3C0F, 0xF03C3C0F, 0x00000000],
    [0x28000028, 0x80000002, 0x80000002, 0x00000000, 0x00024000, 0x00938280, 0x13DBE2C4, 0x77EFFBDD,
     0x77EFFBDD, 0x10DBC2C4, 0x00938280, 0x00024000, 0x00000000, 0x80000002, 0x80000002, 0x28000028],
    [0x00000000, 0x00000000, 0x0E900000, 0x0FFE0000, 0x005F9000, 0x0002F400, 0x0E507D00, 0x0FF41E00,
     0x017E0B80, 0x000B43C0, 0x0143D1D0, 0x0BD1D1E0, 0x0FD1E0E0, 0x0780F0F0, 0x00000000, 0x00000000],
    [0x00000000, 0x001AA400, 0x007FFD00, 0x01F41F40, 0x00500780, 0x000003C0, 0x000003C0, 0x00000140,
     0x0BFFFFE0, 0x0FFEBFF0, 0x0FFC3FF0, 0x0FFD7FF0, 0x0FFC3FF0, 0x0FFD7FF0, 0x0BFFFFE0, 0x00000000],
    [0x00000000, 0x00080000, 0x04080000, 0x02400000, 0x010BE000, 0x0024F800, 0x2890FE00, 0x00C0FF00,
     0x00C0FF00, 0x2890FE00, 0x0024F800, 0x010BE000, 0x02400000, 0x04080000, 0x00080000, 0x00000000],
    [0x06AAAA90, 0x00800200, 0x00800200, 0x00800200, 0x00400100, 0x00100400, 0x000FF000, 0x0003C000,
     0x0003C000, 0x00055000, 0x00114400, 0x00414100, 0x00814200, 0x00E41B00, 0x00800200, 0x06AAAA90],
  ];

  function mix(a, b, t) {
    const pa = parseInt(a.slice(1), 16);
    const pb = parseInt(b.slice(1), 16);
    const ra = (pa >> 16) & 255, ga = (pa >> 8) & 255, ba = pa & 255;
    const rb = (pb >> 16) & 255, gb = (pb >> 8) & 255, bb = pb & 255;
    return "rgb(" +
      Math.round(ra + (rb - ra) * t) + "," +
      Math.round(ga + (gb - ga) * t) + "," +
      Math.round(ba + (bb - ba) * t) + ")";
  }

  function bounds(rows) {
    let x0 = 16, y0 = 16, x1 = -1, y1 = -1;
    for (let sy = 0; sy < 16; sy++) {
      const row = rows[sy] >>> 0;
      for (let sx = 0; sx < 16; sx++) {
        if (!((row >>> (30 - sx * 2)) & 3)) continue;
        if (sx < x0) x0 = sx;
        if (sy < y0) y0 = sy;
        if (sx > x1) x1 = sx;
        if (sy > y1) y1 = sy;
      }
    }
    return x1 >= x0 ? { x0, y0, x1, y1 } : null;
  }

  function drawStamp(canvas, id, on) {
    if (!canvas) return;
    const rows = BITS[id];
    const ctx = canvas.getContext("2d");
    const W = canvas.width;
    const H = canvas.height;
    ctx.clearRect(0, 0, W, H);
    if (!rows) return;
    const color = on ? "#00cfd8" : "#006b70";
    const dim = mix(color, "#000000", 0.45);
    const hi = mix(color, "#ffffff", 0.55);
    const s = 28;
    const cx = W / 2;
    const cy = H / 2;
    let x = cx - s / 2;
    let y = cy - s / 2;
    const box = bounds(rows);
    if (box) {
      x = cx - ((box.x0 + box.x1 + 1) * s + 16) / 32;
      y = cy - ((box.y0 + box.y1 + 1) * s + 16) / 32;
    }
    for (let sy = 0; sy < 16; sy++) {
      const row = rows[sy] >>> 0;
      const dy = y + (sy * s) / 16;
      let h = y + ((sy + 1) * s) / 16 - dy;
      if (h < 1) h = 1;
      for (let sx = 0; sx < 16; sx++) {
        const p = (row >>> (30 - sx * 2)) & 3;
        if (!p) continue;
        const dx = x + (sx * s) / 16;
        let w = x + ((sx + 1) * s) / 16 - dx;
        if (w < 1) w = 1;
        ctx.fillStyle = p === 1 ? dim : p === 3 ? hi : color;
        ctx.fillRect(Math.round(dx), Math.round(dy), Math.max(1, Math.round(w)), Math.max(1, Math.round(h)));
      }
    }
  }

  window.TamafiIcons = { drawStamp };
})();
