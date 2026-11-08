/* TamaFi 240x240 face — same poses and primitives as firmware/Tamafi/face.cpp */
(() => {
  const SIZE = 240;
  const C = {
    black: 0x0000,
    white: 0xffde,
    eye: 0xef7d,
    sheen: 0xffff,
    heart: 0xfcd3,
    heartDeep: 0xf00a,
    blush: 0xc9aa,
    tear: 0x7dff,
    brow: 0xd6ba,
    lid: 0x4a69,
    spark: 0xef7d,
    alert: 0xe36a,
    warn: 0xf50c,
  };

  const MOOD = {
    idle: 0, happy: 1, sad: 2, angry: 3, wow: 4, love: 5, huh: 6, bored: 7,
    scared: 8, excited: 9, shy: 10, laugh: 11, wink: 12, dizzy: 13, cry: 14,
    sleep: 15, listen: 16, think: 17, speak: 18, blink: 19, deauth: 20,
    probe: 21, open: 22,
  };

  function clamp(v, lo, hi) { return v < lo ? lo : v > hi ? hi : v; }
  function mix(a, b, t) {
    t = clamp(t, 0, 1);
    const r1 = (a >> 11) & 31, g1 = (a >> 5) & 63, b1 = a & 31;
    const r2 = (b >> 11) & 31, g2 = (b >> 5) & 63, b2 = b & 31;
    return ((r1 + ((r2 - r1) * t) | 0) << 11) | ((g1 + ((g2 - g1) * t) | 0) << 5) | (b1 + ((b2 - b1) * t) | 0);
  }
  function css(c) {
    const r = ((c >> 11) & 31) * 255 / 31;
    const g = ((c >> 5) & 63) * 255 / 63;
    const b = (c & 31) * 255 / 31;
    return "rgb(" + (r | 0) + "," + (g | 0) + "," + (b | 0) + ")";
  }

  function poseFor(mood) {
    const p = { gazeX: 0, gazeY: 0, eyeW: 54, eyeH: 54, gap: 30, tilt: 0, brow: 0, browTilt: 0, mouth: 0, mouthOpen: 0, faceY: 0, bounce: 0, blush: 0, style: 0, dim: 0 };
    if (mood === MOOD.happy) { p.eyeH = 24; p.mouth = 1; p.mouthOpen = 0.55; p.bounce = 1; p.faceY = -3; }
    else if (mood === MOOD.sad) { p.tilt = -0.32; p.brow = 0.95; p.browTilt = -0.8; p.mouth = 3; p.mouthOpen = 0.3; p.faceY = 6; p.dim = 0.08; }
    else if (mood === MOOD.angry) { p.eyeW = 50; p.eyeH = 42; p.tilt = 0.62; p.brow = 1.1; p.browTilt = 1.1; p.mouth = 6; p.mouthOpen = 0.35; }
    else if (mood === MOOD.wow) { p.eyeW = 60; p.eyeH = 60; p.gap = 28; p.brow = 0.7; p.mouth = 4; p.mouthOpen = 0.9; p.faceY = -5; }
    else if (mood === MOOD.love) { p.style = 1; p.eyeW = 48; p.eyeH = 48; p.mouth = 1; p.mouthOpen = 0.4; p.blush = 0.85; p.bounce = 0.7; }
    else if (mood === MOOD.huh) { p.brow = 0.95; p.browTilt = 1.05; p.tilt = 0.18; p.mouth = 4; p.mouthOpen = 0.22; }
    else if (mood === MOOD.bored) { p.eyeH = 20; p.gazeX = 10; p.mouth = 6; p.mouthOpen = 0.12; p.faceY = 5; p.dim = 0.12; }
    else if (mood === MOOD.scared) { p.eyeW = 58; p.eyeH = 58; p.gap = 28; p.mouth = 4; p.mouthOpen = 0.55; p.brow = 0.55; p.browTilt = -0.35; }
    else if (mood === MOOD.excited) { p.eyeH = 32; p.mouth = 1; p.mouthOpen = 0.8; p.bounce = 1.5; p.faceY = -5; }
    else if (mood === MOOD.shy) { p.eyeH = 38; p.gazeY = 9; p.gazeX = 7; p.blush = 1; p.mouth = 1; p.mouthOpen = 0.18; p.faceY = 8; }
    else if (mood === MOOD.laugh) { p.eyeH = 20; p.mouth = 1; p.mouthOpen = 0.82; p.blush = 0.28; p.bounce = 1.15; p.faceY = -4; }
    else if (mood === MOOD.wink) { p.mouth = 1; p.mouthOpen = 0.38; p.tilt = 0.14; p.blush = 0.16; }
    else if (mood === MOOD.dizzy) { p.style = 3; p.mouth = 4; p.mouthOpen = 0.32; p.tilt = 0.28; p.eyeW = 56; p.eyeH = 56; p.brow = 0.35; }
    else if (mood === MOOD.cry) { p.eyeH = 46; p.tilt = -0.36; p.brow = 1; p.browTilt = -0.85; p.mouth = 3; p.mouthOpen = 0.5; p.faceY = 8; p.dim = 0.1; }
    else if (mood === MOOD.sleep) { p.eyeH = 8; p.faceY = 7; p.dim = 0.28; }
    else if (mood === MOOD.listen) { p.eyeW = 64; p.eyeH = 40; p.gap = 30; p.tilt = 0.22; p.brow = 0.72; p.browTilt = 0.9; p.gazeX = 4; p.gazeY = -2; p.mouth = 6; p.mouthOpen = 0.1; }
    else if (mood === MOOD.think) { p.brow = 0.9; p.browTilt = 0.7; p.tilt = 0.1; p.mouth = 6; p.mouthOpen = 0.1; }
    else if (mood === MOOD.speak) { p.mouth = 2; p.mouthOpen = 0.75; }
    else if (mood === MOOD.deauth) { p.eyeW = 56; p.eyeH = 56; p.gap = 28; p.brow = 0.85; p.browTilt = 0.45; p.mouth = 6; p.mouthOpen = 0.18; p.faceY = -2; }
    else if (mood === MOOD.probe) { p.eyeW = 60; p.eyeH = 28; p.gap = 30; p.brow = 0.75; p.browTilt = 0.35; p.mouth = 6; p.mouthOpen = 0.12; p.faceY = -3; }
    else if (mood === MOOD.open) { p.eyeW = 50; p.eyeH = 28; p.brow = 0.7; p.browTilt = -0.25; p.mouth = 6; p.mouthOpen = 0.18; p.faceY = 3; p.gazeX = 8; p.gazeY = 4; p.blush = 0.35; }
    return p;
  }

  function lerpPose(a, b, k) {
    const o = {};
    Object.keys(b).forEach((key) => { o[key] = a[key] + (b[key] - a[key]) * k; });
    return o;
  }

  const G = {
    ctx: null,
    mood: MOOD.idle,
    live: poseFor(MOOD.idle),
    target: poseFor(MOOD.idle),
    lidL: 1,
    lidR: 1,
    blinkT: 1,
    nextBlink: 0,
    nextLook: 0,
    lookX: 0,
    lookY: 0,
    shakeX: 0,
    dizzyKind: 1,
    running: false,
  };

  function roundRect(x, y, w, h, r, color) {
    const c = G.ctx;
    if (w < 1 || h < 1) return;
    r = Math.max(1, Math.min(r, w / 2, h / 2));
    c.fillStyle = css(color);
    c.beginPath();
    if (c.roundRect) c.roundRect(x, y, w, h, r);
    else c.rect(x, y, w, h);
    c.fill();
  }
  function fillCirc(x, y, r, color) {
    if (r < 0.5) return;
    G.ctx.fillStyle = css(color);
    G.ctx.beginPath();
    G.ctx.arc(x, y, r, 0, Math.PI * 2);
    G.ctx.fill();
  }
  function fillOval(cx, cy, rx, ry, color) {
    if (rx < 1 || ry < 1) return;
    if (Math.abs(rx - ry) <= 1) return fillCirc(cx, cy, rx, color);
    G.ctx.fillStyle = css(color);
    G.ctx.beginPath();
    G.ctx.ellipse(cx, cy, rx, ry, 0, 0, Math.PI * 2);
    G.ctx.fill();
  }
  function fillTri(x1, y1, x2, y2, x3, y3, color) {
    G.ctx.fillStyle = css(color);
    G.ctx.beginPath();
    G.ctx.moveTo(x1, y1); G.ctx.lineTo(x2, y2); G.ctx.lineTo(x3, y3); G.ctx.closePath(); G.ctx.fill();
  }
  function hline(x, y, w, color) {
    G.ctx.fillStyle = css(color);
    G.ctx.fillRect(x, y, w, 1);
  }
  function vline(x, y, h, color) {
    G.ctx.fillStyle = css(color);
    G.ctx.fillRect(x, y, 1, h);
  }

  function cornerR(w, h) {
    let r = 10;
    const cap = Math.min(w, h) / 3;
    if (r > cap) r = cap;
    if (r < 2) r = 2;
    return r | 0;
  }

  function eyeColor(p, now) {
    let c = C.eye;
    if (G.mood === MOOD.love) c = mix(C.eye, C.heart, 0.45);
    else if (G.mood === MOOD.angry) c = mix(C.eye, 0xfeee, 0.2);
    else if (G.mood === MOOD.sleep) c = mix(C.eye, 0x7bef, 0.35);
    else if (G.mood === MOOD.scared) c = C.white;
    else if (G.mood === MOOD.deauth) c = mix(C.white, C.alert, ((now / 88) | 0) % 8 >= 6 ? 0.55 : 0.16);
    else if (G.mood === MOOD.probe) {
      const pms = now % 1600;
      c = mix(C.eye, C.warn, pms < 130 || (pms > 800 && pms < 930) ? 0.45 : 0.18);
    }
    return mix(c, 0, p.dim);
  }

  function drawGlow(x, y, w, h, r, eye, open) {
    if (open < 0.42 || w < 4 || h < 8) return;
    const fade = clamp((open - 0.42) / 0.58, 0, 1);
    const cover = Math.round((1 - open) * h);
    const gy = y + cover, gh = h - cover;
    if (gh < 10) return;
    roundRect(x - 7, gy - 3, w + 14, gh + 10, r + 4, mix(eye, 0, 1 - 0.12 * fade));
    roundRect(x - 4, gy - 1, w + 8, gh + 5, r + 2, mix(eye, 0, 1 - 0.22 * fade));
    roundRect(x - 2, gy, w + 4, gh + 2, r + 1, mix(eye, 0, 1 - 0.38 * fade));
  }

  function drawSquareEye(cx, cy, w, h, eye, lookX, lookY) {
    if (w < 2 || h < 2) return;
    const x = cx - w / 2, y = cy - h / 2, r = cornerR(w, h);
    if (h <= 6) {
      roundRect(x, cy - 2, w, 4, 2, mix(eye, 0, 0.15));
      return;
    }
    roundRect(x, y, w, h, r, eye);
    if (h > 18) {
      const hs = Math.max(3, Math.min(w, h) / 9);
      fillCirc(cx - w / 5 + lookX * 0.25, cy - h / 5 + lookY * 0.25, hs, C.sheen);
    }
  }

  function drawLid(cx, cy, w, h, open) {
    if (h <= 6) return;
    const closed = 1 - clamp(open, 0, 1);
    let cover = Math.round(closed * (h - 3));
    if (cover < 1) return;
    const x = cx - w / 2, top = cy - h / 2, r = cornerR(w, h);
    G.ctx.fillStyle = css(0);
    G.ctx.fillRect(x - 10, top - 12, w + 20, cover + 12);
    roundRect(x - 1, top - r - 1, w + 2, cover + r + 1, r, 0);
    if (cover < h - 4) roundRect(x + 3, top + cover - 1, w - 6, 2, 1, C.lid);
  }

  function drawHeart(cx, cy, s, color, lookX, lookY) {
    const r = Math.max(6, s / 3);
    fillCirc(cx - r + 1, cy - r / 3, r + 2, mix(color, 0, 0.7));
    fillCirc(cx + r - 1, cy - r / 3, r + 2, mix(color, 0, 0.7));
    fillCirc(cx - r + 1, cy - r / 3, r, color);
    fillCirc(cx + r - 1, cy - r / 3, r, color);
    fillTri(cx - s / 2, cy + 1, cx + s / 2, cy + 1, cx, cy + s / 2 + 1, color);
    fillCirc(cx - r / 2 + lookX * 0.2, cy - r / 2 + lookY * 0.2, Math.max(2, r / 4), C.sheen);
  }

  function drawBrow(cx, top, w, amount, tilt, side) {
    if (amount < 0.08) return;
    const bw = Math.max(14, w * 0.68);
    const bh = Math.max(3, 3.4 * amount);
    roundRect(cx - bw / 2 + tilt * 4 * side, top - 12 - bh + tilt * 7 * side, bw, bh, bh / 2, mix(C.brow, 0, 0.12));
  }

  function drawMouth(p, cx, below, color) {
    if (p.mouth < 0.12) return;
    const y = below + 18;
    if (p.mouth < 1.5) {
      const rw = 16 + 8 * p.mouthOpen, rh = 5 + 3 * p.mouthOpen;
      fillOval(cx, y + 3, rw, rh, color);
      fillOval(cx, y - 2, rw + 2, rh + 1, 0);
      return;
    }
    if (p.mouth < 2.5) {
      const rw = 8 + 9 * p.mouthOpen, rh = 3 + 8 * p.mouthOpen;
      fillOval(cx, y, rw, rh, color);
      if (rh > 6) fillOval(cx, y + 1, Math.max(2, rw - 5), Math.max(2, rh - 5), 0);
      return;
    }
    if (p.mouth < 3.5) {
      fillOval(cx, y - 3, 13, 5, color);
      fillOval(cx, y + 2, 15, 6, 0);
      return;
    }
    if (p.mouth < 4.5) {
      const r = 5 + 7 * p.mouthOpen;
      fillCirc(cx, y, r, color);
      fillCirc(cx, y + 1, Math.max(2, r - 4), 0);
      return;
    }
    roundRect(cx - 14, y - 1, 28, 4, 2, mix(color, 0, 0.08));
  }

  function drawBlush(cx, cy, amt) {
    if (amt < 0.15) return;
    for (let i = 3; i >= 0; i--) fillOval(cx, cy, 14 + i * 2, 7 + i, mix(C.blush, 0, 0.55 + i * 0.12 - amt * 0.2));
  }

  function drawTear(cx, cy, t, phase) {
    const drop = 0.5 + 0.5 * Math.sin(t * 2.1 + phase);
    const y = cy + 4 + drop * 12;
    const c = mix(C.tear, 0, 0.15);
    fillCirc(cx, y, 3, c);
    fillTri(cx - 3, y, cx + 3, y, cx, y - 7, c);
    fillCirc(cx - 1, y - 1, 1, C.sheen);
  }

  function drawSpark(x, y, s, a) {
    const c = mix(C.spark, 0, 0.15 + 0.25 * (0.5 - 0.5 * a));
    hline(x - s, y, s * 2 + 1, c);
    vline(x, y - s, s * 2 + 1, c);
  }

  function drawLetterZ(x, y, s, color) {
    if (s < 5) s = 5;
    const t = Math.max(1, (s / 5) | 0);
    G.ctx.fillStyle = css(color);
    G.ctx.fillRect(x, y, s, t);
    G.ctx.fillRect(x, y + s - t, s, t);
    G.ctx.beginPath();
    G.ctx.moveTo(x + s - 1, y + t);
    G.ctx.lineTo(x, y + s - t);
    G.ctx.strokeStyle = css(color);
    G.ctx.lineWidth = 2;
    G.ctx.stroke();
  }

  function drawZzz(x, y, t) {
    for (let i = 0; i < 3; i++) {
      const u = ((t * 0.32 + i * 0.33) % 1 + 1) % 1;
      const s = 12 - (u * 6 | 0);
      const px = x + (u * 24) - s / 2;
      const py = y - (u * 30) - s / 2;
      if (px < 1 || py < 1 || px + s > SIZE - 1 || py + s > SIZE - 1) continue;
      drawLetterZ(px, py, s, mix(C.eye, 0, u * 0.85));
    }
  }

  function dotsArc(cx, cy, r, a0, a1, step, color) {
    G.ctx.fillStyle = css(color);
    for (let a = a0; a <= a1; a += step) {
      const rad = a * 0.017453;
      const px = cx + Math.cos(rad) * r;
      const py = cy + Math.sin(rad) * r;
      if (px < 2 || px > SIZE - 3 || py < 2 || py > SIZE - 3) continue;
      G.ctx.beginPath();
      G.ctx.arc(px, py, 1, 0, Math.PI * 2);
      G.ctx.fill();
    }
  }

  function drawListenArcs(x, y, t, c) {
    x = clamp(x, 18, SIZE - 28);
    y = clamp(y, 18, SIZE - 18);
    const pulse = 0.5 + 0.5 * Math.sin(t * 3.6);
    for (let i = 0; i < 2; i++) {
      const vis = pulse - i * 0.28;
      if (vis < 0.18) continue;
      dotsArc(x, y, 7 + i * 6, -40, 40, 10, mix(c, 0, 0.22 + i * 0.14));
    }
  }

  function drawQuestion(x, y, c) {
    fillCirc(x, y - 7, 5, c);
    fillCirc(x, y - 7, 2, 0);
    roundRect(x + 2, y - 6, 3, 7, 1, c);
    fillCirc(x, y + 6, 2, c);
  }

  function drawWifiMark(x, y, c) {
    x = clamp(x, 20, SIZE - 20);
    y = Math.max(18, y);
    fillCirc(x, y + 16, 2, c);
    for (let i = 0; i < 3; i++) dotsArc(x, y + 16, 6 + i * 6, 200, 340, 10, c);
  }

  function drawRadarSweep(x, y, now, c) {
    x = clamp(x, 28, SIZE - 28);
    y = Math.max(16, y);
    const cy = y + 18;
    fillCirc(x, cy, 2, c);
    for (let i = 0; i < 3; i++) dotsArc(x, cy, 8 + i * 8, 200, 340, 10, mix(c, 0, 0.28 + i * 0.16));
    const u = airProbeSweep(now);
    const ang = (200 + u * 140) * 0.017453;
    const tx = x + Math.cos(ang) * 24;
    const ty = cy + Math.sin(ang) * 24;
    G.ctx.strokeStyle = css(mix(C.warn, c, 0.25));
    G.ctx.lineWidth = 1;
    G.ctx.beginPath();
    G.ctx.moveTo(x, cy);
    G.ctx.lineTo(tx, ty);
    G.ctx.stroke();
    fillCirc(tx, ty, 2, C.warn);
  }

  function drawOpenLock(x, y, c) {
    x = clamp(x, 16, SIZE - 16);
    y = Math.max(22, y);
    roundRect(x - 8, y, 16, 13, 2, c);
    roundRect(x - 4, y + 4, 8, 6, 1, 0);
    G.ctx.strokeStyle = css(c);
    G.ctx.lineWidth = 2;
    G.ctx.beginPath();
    G.ctx.moveTo(x + 5, y);
    G.ctx.lineTo(x + 5, y - 9);
    G.ctx.lineTo(x - 3, y - 12);
    G.ctx.lineTo(x - 8, y - 7);
    G.ctx.stroke();
  }

  function drawOrbitSheen(cx, cy, w, h, a, eye) {
    const hs = Math.max(4, Math.min(w, h) / 7);
    fillCirc(cx + Math.cos(a - 0.65) * w * 0.2, cy + Math.sin(a - 0.65) * h * 0.2, Math.max(2, hs - 1), mix(C.sheen, eye, 0.58));
    fillCirc(cx + Math.cos(a) * w * 0.2, cy + Math.sin(a) * h * 0.2, hs, C.sheen);
  }

  function airGlitchHit(now) { return ((now / 88) | 0) % 8 >= 6; }
  function airProbeSweep(now) {
    const t = (now % 1600) / 1600;
    return t < 0.5 ? t * 2 : 2 - t * 2;
  }
  function airProbePing(now) {
    const p = now % 1600;
    return p < 130 || (p > 800 && p < 930);
  }

  function blinkCurve(t) {
    if (t >= 1) return 1;
    if (t < 0.28) { const u = t / 0.28; return 1 - u * u * u; }
    if (t < 0.38) return 0.06;
    const u = (t - 0.38) / 0.62;
    const e = 1 - (1 - u) * (1 - u);
    return 0.06 + 0.94 * e;
  }

  function applyMoodMotion(now) {
    const t = poseFor(G.mood);
    t.gazeX = G.lookX;
    t.gazeY = G.lookY;
    const tsec = now / 1000;
    const breath = Math.sin(tsec * 1.15);
    if (G.mood === MOOD.speak) {
      t.mouthOpen = 0.22 + 0.72 * Math.abs(Math.sin(tsec * 12.5));
      t.faceY = Math.sin(tsec * 8) * 2.4;
    } else if (G.mood === MOOD.happy) t.faceY = -3 + Math.sin(tsec * 5.2) * 2.6;
    else if (G.mood === MOOD.laugh) t.faceY = -4 + Math.sin(tsec * 5) * 2.8;
    else if (G.mood === MOOD.excited) t.faceY = -5 + Math.sin(tsec * 9.5) * 4.8;
    else if (G.mood === MOOD.sleep) t.faceY = 7 + breath * 2.4;
    else if (G.mood === MOOD.listen) {
      t.eyeW = 64 + Math.sin(tsec * 4.4) * 2.5;
      t.eyeH = 40 + Math.sin(tsec * 4.4) * 1.6;
      t.tilt = 0.22 + Math.sin(tsec * 2) * 0.05;
      t.gazeX = 4 + Math.sin(tsec * 1.7) * 2;
      t.brow = 0.7 + 0.1 * (0.5 + 0.5 * Math.sin(tsec * 2.4));
    } else if (G.mood === MOOD.scared) {
      t.gazeX += Math.sin(tsec * 22) * 1.2;
      t.faceY = Math.sin(tsec * 21) * 1.1;
    } else if (G.mood === MOOD.deauth) {
      t.gazeX = 0; t.gazeY = 0; t.eyeW = 56; t.eyeH = 56; t.mouthOpen = 0.18; t.faceY = -2;
      if (airGlitchHit(now)) {
        const dir = ((now / 88) | 0) & 1 ? 5 : -5;
        G.shakeX = dir;
        t.dim = 0.22;
        t.gazeX = dir * 0.4;
      } else { G.shakeX = 0; t.dim = 0; }
    } else if (G.mood === MOOD.probe) {
      const u = airProbeSweep(now);
      t.gazeX = -13 + u * 26;
      t.gazeY = -2 - Math.sin(u * Math.PI) * 2;
      t.tilt = -0.14 + u * 0.28;
      t.faceY = -3;
      t.eyeW = airProbePing(now) ? 66 : 60;
      t.eyeH = airProbePing(now) ? 22 : 28;
    } else if (G.mood === MOOD.open) {
      t.gazeX = 8 + Math.sin(tsec * 1.5) * 2.2;
      t.gazeY = 5 + Math.sin(tsec * 1.2) * 1.4;
      t.faceY = 3 + breath * 0.9;
      t.blush = 0.32 + 0.18 * (0.5 + 0.5 * Math.sin(tsec * 2.1));
    } else if (G.mood === MOOD.angry) t.faceY = Math.sin(tsec * 13) * 0.9;
    else if (G.mood === MOOD.love) t.faceY = Math.sin(tsec * 3.4) * 2.4;
    else if (G.mood === MOOD.dizzy) {
      const spin = G.dizzyKind === 1 ? 2 : G.dizzyKind === 3 ? 3.2 : 4.2;
      const amp = G.dizzyKind === 2 ? 8 : 6.5;
      t.gazeX = Math.sin(tsec * spin) * amp;
      t.gazeY = Math.cos(tsec * spin * 0.84) * 4.5;
      t.tilt = 0.2 + Math.sin(tsec * 4.6) * 0.14;
      t.faceY = Math.sin(tsec * 5.8) * 2.4;
      t.mouthOpen = 0.3 + 0.1 * (0.5 + 0.5 * Math.sin(tsec * 5.2));
      G.shakeX = Math.sin(tsec * 13) * (G.dizzyKind === 1 ? 1.4 : 2.4);
    } else if (G.mood === MOOD.cry) t.faceY = 8 + Math.sin(tsec * 6.5) * 1.1;
    else if (G.mood === MOOD.wow) {
      t.eyeW = 60 + Math.sin(tsec * 6.5) * 1.1;
      t.eyeH = 60 + Math.sin(tsec * 6.5) * 1.1;
    } else {
      t.faceY += breath * 1.15;
    }
    G.target = t;
  }

  function drawFrame(now) {
    const c = G.ctx;
    c.fillStyle = "#000";
    c.fillRect(0, 0, SIZE, SIZE);
    const p = G.live;
    const eye = eyeColor(p, now);
    const cx = SIZE / 2;
    const cy = SIZE / 2 - 4 + Math.round(p.faceY);
    const w = p.eyeW;
    const h = Math.max(4, p.eyeH);
    const gap = Math.max(28, p.gap);
    const split = (w + gap) * 0.5;
    const lookX = clamp(p.gazeX, -22, 22);
    const lookY = clamp(p.gazeY, -16, 16);
    let lx = Math.round(cx - (split - lookX) + G.shakeX);
    let rx = Math.round(cx + (split + lookX) + G.shakeX);
    const ey = cy + Math.round(lookY);
    const ly = ey + Math.round(-p.tilt * 5);
    const ry = ey + Math.round(p.tilt * 5);
    const minDist = Math.round(w + 28);
    if ((rx - lx) < minDist) {
      const mid = (lx + rx) / 2;
      lx = mid - minDist / 2;
      rx = mid + minDist / 2;
    }
    const iw = w | 0, ih = h | 0;
    const half = iw / 2 + 8;
    const pad = 10;
    if (lx - half < pad) { const d = pad - (lx - half); lx += d; rx += d; }
    if (rx + half > SIZE - pad) { const d = (rx + half) - (SIZE - pad); lx -= d; rx -= d; }
    const r = cornerR(iw, ih);
    let lidL = G.lidL, lidR = G.lidR;
    if (G.mood === MOOD.sleep) lidL = lidR = 0.04;
    if (G.mood === MOOD.wink) lidR = 0.06;
    if (G.mood === MOOD.laugh) lidL = lidR = 0.22;
    if (G.mood === MOOD.probe) lidL = lidR = airProbePing(now) ? 1 : 0.88;

    if (!(G.mood === MOOD.wink && lidL < 0.55)) drawGlow(lx - iw / 2, ly - ih / 2, iw, ih, r, eye, lidL);
    drawGlow(rx - iw / 2, ry - ih / 2, iw, ih, r, eye, lidR);
    drawBlush(lx, ly + ih / 2 + 6, p.blush);
    drawBlush(rx, ry + ih / 2 + 6, p.blush);

    if (G.mood === MOOD.dizzy) {
      roundRect(lx - iw / 2, ly - ih / 2, iw, ih, r, eye);
      roundRect(rx - iw / 2, ry - ih / 2, iw, ih, r, eye);
      const a = now / 1000 * 4.2;
      drawOrbitSheen(lx, ly, iw, ih, a, eye);
      drawOrbitSheen(rx, ry, iw, ih, -a, eye);
      drawLid(lx, ly, iw, ih, lidL);
      drawLid(rx, ry, iw, ih, lidR);
    } else if (G.mood === MOOD.love && p.style > 0.5) {
      drawHeart(lx, ly, iw, mix(C.heart, C.heartDeep, 0.25), lookX, lookY);
      drawHeart(rx, ry, iw, mix(C.heart, C.heartDeep, 0.25), lookX, lookY);
    } else {
      drawSquareEye(lx, ly, iw, ih, eye, lookX, lookY);
      drawSquareEye(rx, ry, iw, ih, eye, lookX, lookY);
      drawLid(lx, ly, iw, ih, lidL);
      drawLid(rx, ry, iw, ih, lidR);
    }
    const top = ey - ih / 2;
    drawBrow(lx, top, iw, p.brow, p.browTilt, -1);
    drawBrow(rx, top, iw, p.brow, p.browTilt, 1);
    drawMouth(p, cx, ey + ih / 2, eye);
    const t = now / 1000;
    if (G.mood === MOOD.sad || G.mood === MOOD.cry) {
      drawTear(lx, ey + ih / 2 + 1, t, 0);
      if (G.mood === MOOD.cry) drawTear(rx, ey + ih / 2 + 1, t, 1.15);
    }
    if (G.mood === MOOD.excited || G.mood === MOOD.love) {
      const pulse = 0.5 + 0.5 * Math.sin(t * 5.5);
      drawSpark(lx - 20, ly - 16, 5, pulse);
      drawSpark(rx + 20, ry - 20, 4, 1 - pulse);
    }
    if (G.mood === MOOD.listen) drawListenArcs(rx + iw / 2 + 8, ry, t, eye);
    if (G.mood === MOOD.huh) drawQuestion(rx + iw / 2 + 14, ry - ih / 2 - 10, mix(eye, 0, 0.08));
    if (G.mood === MOOD.sleep) drawZzz(rx + 16, ry - 16, t);
    if (G.mood === MOOD.probe) drawRadarSweep(cx, 10, now, mix(C.warn, eye, 0.2));
    if (G.mood === MOOD.open) {
      drawWifiMark(cx, 16, mix(eye, C.warn, 0.35));
      drawOpenLock(rx + iw / 2 + 16, ry - 8, mix(C.warn, eye, 0.2));
    }
  }

  function loop(now) {
    if (!G.running) return;
    applyMoodMotion(now);
    G.live = lerpPose(G.live, G.target, 0.18);
    if (G.mood !== MOOD.sleep && G.mood !== MOOD.wink && G.mood !== MOOD.laugh) {
      if (!G.nextBlink) G.nextBlink = now + 2800;
      if (now >= G.nextBlink) {
        G.blinkT = 0;
        G.nextBlink = now + 2800 + Math.random() * 2200;
      }
      if (G.blinkT < 1) G.blinkT = Math.min(1, G.blinkT + 0.08);
      const b = blinkCurve(G.blinkT);
      G.lidL = b;
      G.lidR = b;
    }
    const saccadeMoods = [MOOD.idle, MOOD.think, MOOD.bored, MOOD.shy, MOOD.sad, MOOD.happy, MOOD.excited, MOOD.huh];
    if (saccadeMoods.indexOf(G.mood) >= 0 && now >= G.nextLook) {
      G.lookX = Math.random() * 20 - 10;
      G.lookY = Math.random() * 12 - 6;
      G.nextLook = now + 1800 + Math.random() * 2400;
    }
    drawFrame(now);
    requestAnimationFrame(loop);
  }

  window.TamafiFace = {
    attach(canvas) {
      canvas.width = SIZE;
      canvas.height = SIZE;
      G.ctx = canvas.getContext("2d");
      G.ctx.imageSmoothingEnabled = false;
      G.running = true;
      requestAnimationFrame(loop);
    },
    setMood(name) {
      const key = String(name || "idle").toLowerCase();
      const aliases = { surprised: "wow", confused: "huh", attack: "deauth", noise: "probe" };
      const mapped = aliases[key] || key;
      const id = MOOD[mapped] !== undefined ? MOOD[mapped] : MOOD.idle;
      G.mood = id;
      G.target = poseFor(id);
      G.nextBlink = 0;
      G.shakeX = 0;
      if (id === MOOD.dizzy) G.dizzyKind = 1 + ((Math.random() * 3) | 0);
      if (id === MOOD.idle || id === MOOD.think || id === MOOD.bored) G.nextLook = 0;
      else if (id !== MOOD.listen && id !== MOOD.open && id !== MOOD.probe && id !== MOOD.dizzy) {
        G.lookX = poseFor(id).gazeX;
        G.lookY = poseFor(id).gazeY;
      }
    },
    mood() { return G.mood; },
  };
})();
