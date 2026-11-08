(() => {
  const MOODS = [
    ["idle", "Idle"], ["happy", "Happy"], ["sad", "Sad"], ["angry", "Angry"],
    ["wow", "Wow"], ["love", "Love"], ["huh", "Huh"], ["bored", "Bored"],
    ["scared", "Scared"], ["excited", "Excited"], ["shy", "Shy"], ["laugh", "Laugh"],
    ["wink", "Wink"], ["dizzy", "Dizzy"], ["cry", "Cry"], ["sleep", "Sleep"],
    ["listen", "Listen"], ["think", "Think"], ["speak", "Speak"], ["blink", "Blink"],
    ["deauth", "Deauth"], ["probe", "Probe"], ["open", "Open"],
  ];

  const STAMP_META = [
    "ONLINE", "HUNTER", "NAP TIME", "GROWN", "HOSTILE", "STATIC",
    "HEARD", "CROWDED", "OPEN AIR", "DAY ONE", "SURVIVOR",
  ];

  const PAGES = {
    device: ["Device", "Face, identity, and remote buttons"],
    pet: ["Pet", "Hunger, mood, and growth"],
    air: ["Environment", "What the radios are hearing"],
    sys: ["System", "Heap, firmware, and mood preview"],
    stamps: ["Stamps", "Unlocked marks"],
    log: ["Log", "Packet and system events"],
    set: ["Settings", "Sleep, sound, brightness, and theme"],
    update: ["Update", "Install Face firmware from a .bin"],
  };

  const state = {
    connected: false,
    applying: false,
    hasBin: false,
    previewing: false,
    lastFace: "idle",
    lastActivity: "—",
    needFw: false,
    vitals: { hunger: 0, happy: 0, energy: 0, health: 0 },
  };

  const $ = (id) => document.getElementById(id);

  function showPage(id) {
    const meta = PAGES[id] || PAGES.device;
    document.body.dataset.page = id;
    $("pageTitle").textContent = meta[0];
    $("pageSub").textContent = meta[1];
    document.querySelectorAll(".tab").forEach((b) => b.classList.toggle("on", b.dataset.tab === id));
    document.querySelectorAll(".sheet").forEach((s) => s.classList.toggle("on", s.id === "tab-" + id));
  }

  async function call(path, body) {
    const api = window.pywebview && window.pywebview.api;
    const name = String(path || "").replace(/^\/api\//, "");
    if (api) {
      try {
        if (name === "ports") return await api.ports();
        if (name === "state") return await api.snapshot();
        if (name === "connect") return await api.connect((body && body.device) || "");
        if (name === "disconnect") return await api.disconnect();
        if (name === "set") return await api.set_value(body.key, body.value);
        if (name === "preview") return await api.preview(body && body.code);
        if (name === "key") return await api.key((body && body.name) || "");
        if (name === "save") return await api.save();
        if (name === "pick_firmware") return await api.pick_firmware();
        if (name === "flash") return await api.flash((body && body.port) || "");
      } catch (err) {}
    }
    const opts = { method: body === undefined ? "GET" : "POST" };
    if (body !== undefined) {
      opts.headers = { "Content-Type": "application/json" };
      opts.body = JSON.stringify(body);
    }
    const r = await fetch(path, opts);
    try {
      return await r.json();
    } catch (err) {
      return {};
    }
  }

  function note(msg) {
    $("note").textContent = msg || "";
  }

  function setLink(mode, text) {
    document.body.dataset.link = mode;
    $("linkText").textContent = text;
    const label = mode === "on" ? "Disconnect" : mode === "busy" ? "Waiting" : "Connect";
    $("connect").textContent = label;
    $("connect").disabled = mode === "busy";
    if ($("connectHero")) {
      if (state.needFw && mode !== "on" && mode !== "busy") {
        $("connectHero").textContent = "Install firmware";
        $("connectHero").disabled = false;
      } else {
        $("connectHero").textContent = label;
        $("connectHero").disabled = mode === "busy" || mode === "on";
      }
    }
    if (mode !== "on" && $("portLabel") && !state.needFw) $("portLabel").textContent = "—";
  }

  function fillPorts(rows) {
    const sel = $("port");
    const prev = sel.value;
    sel.innerHTML = "";
    (rows || []).forEach((row) => {
      const opt = document.createElement("option");
      opt.value = row.device;
      opt.textContent = row.label;
      sel.appendChild(opt);
    });
    if (prev && [...sel.options].some((o) => o.value === prev)) sel.value = prev;
  }

  function setPressed(id, on) {
    const el = $(id);
    el.setAttribute("aria-pressed", on ? "true" : "false");
    el.textContent = on ? "On" : "Off";
  }

  function setSeg(rootId, val) {
    const root = $(rootId);
    [...root.querySelectorAll("button")].forEach((b) => {
      b.classList.toggle("on", b.dataset.val === String(val));
    });
  }

  function ageText(mins) {
    mins = Number(mins) || 0;
    const d = Math.floor(mins / 1440);
    const h = Math.floor((mins / 60) % 24);
    return d + "d " + String(h).padStart(2, "0") + "h";
  }

  function heapText(n) {
    n = Number(n) || 0;
    if (n >= 1024) return Math.round(n / 1024) + "K";
    return String(n);
  }

  function upText(s) {
    s = Number(s) || 0;
    const h = Math.floor(s / 3600);
    const m = Math.floor((s % 3600) / 60);
    const sec = Math.floor(s % 60);
    if (h) return h + "h " + String(m).padStart(2, "0") + "m";
    return m + "m " + String(sec).padStart(2, "0") + "s";
  }

  function rssiText(n) {
    n = Number(n);
    if (!Number.isFinite(n) || n <= -100) return "--";
    return n + " dBm";
  }

  function pipColor(key, v) {
    if (key === "hunger") return v > 75 ? "#e76f51" : "";
    if (key === "happy") return v > 60 ? "#e76f51" : "";
    if (key === "energy") return v < 30 ? "#f4a261" : "";
    if (key === "health") return "#e76f51";
    return "";
  }

  function airColor(name) {
    const a = String(name || "").toUpperCase();
    if (a === "HOSTILE") return "var(--bad)";
    if (a === "CALM") return "var(--good)";
    if (a === "NOISY" || a === "OPEN") return "var(--warn)";
    return "";
  }

  function buildPips() {
    document.querySelectorAll(".bar").forEach((bar) => {
      const box = bar.querySelector(".pips");
      box.innerHTML = "";
      for (let i = 0; i < 14; i++) box.appendChild(document.createElement("i"));
    });
  }

  function drawBar(bar, value) {
    const v = Math.max(0, Math.min(100, Number(value) || 0));
    let filled = Math.round((v * 14 + 50) / 100);
    if (v > 0 && filled === 0) filled = 1;
    if (v === 0) filled = 0;
    const color = pipColor(bar.dataset.key, v);
    bar.style.setProperty("--pip", color || "var(--accent)");
    bar.querySelector("em").textContent = Math.round(v) + "%";
    [...bar.querySelectorAll(".pips i")].forEach((pip, i) => {
      pip.classList.toggle("on", i < filled);
    });
  }

  function buildMoodGrid() {
    const box = $("moodGrid");
    if (!box) return;
    box.innerHTML = "";
    MOODS.forEach(([id, label]) => {
      const btn = document.createElement("button");
      btn.dataset.preview = id;
      btn.textContent = label;
      box.appendChild(btn);
    });
  }

  function setFace(face, caption) {
    const name = String(face || "idle").toLowerCase();
    if (window.TamafiFace) window.TamafiFace.setMood(name);
    $("moodName").textContent = name;
    $("activityName").textContent = String(caption || "—");
  }

  function flashMood(id) {
    document.querySelectorAll("#moodGrid [data-preview]").forEach((b) => {
      b.classList.toggle("flash", b.dataset.preview === id);
    });
    clearTimeout(flashMood._t);
    flashMood._t = setTimeout(() => {
      document.querySelectorAll("#moodGrid .flash").forEach((b) => b.classList.remove("flash"));
    }, 450);
  }

  function triggerPreview(code) {
    flashMood(code);
    state.previewing = true;
    setFace(code, "preview");
    clearTimeout(state.previewTimer);
    state.previewTimer = setTimeout(() => {
      state.previewing = false;
      setFace(state.lastFace, state.lastActivity);
    }, 2000);
  }

  function renderStamps(rows) {
    const box = $("stampCase");
    box.innerHTML = "";
    let have = 0;
    for (let i = 0; i < 11; i++) {
      const row = (rows || []).find((r) => Number(r.id) === i) || (rows || [])[i];
      const on = !!(row && row.have);
      if (on) have += 1;
      const title = STAMP_META[i];
      const el = document.createElement("div");
      el.className = "stamp-cell" + (on ? " on" : "");
      el.innerHTML = "<canvas class=\"mark\" width=\"36\" height=\"36\"></canvas><em>" + (on ? (row.title || title) : "----") + "</em>";
      if (window.TamafiIcons) window.TamafiIcons.drawStamp(el.querySelector("canvas"), i, on);
      box.appendChild(el);
    }
    $("stampCount").textContent = have + "/" + 11;
  }

  function logAge(ms) {
    const t = Number(ms) || 0;
    const mm = String(Math.floor(t / 60000) % 60).padStart(2, "0");
    const ss = String(Math.floor(t / 1000) % 60).padStart(2, "0");
    return mm + ":" + ss;
  }

  function renderLog(rows) {
    const tape = $("tape");
    tape.innerHTML = "";
    if (!rows || !rows.length) {
      tape.innerHTML = "<div class=\"empty\">NO PACKETS YET<small>waiting on air</small></div>";
      return;
    }
    rows.forEach((row, i) => {
      const kind = String(row.kind || "PET").trim();
      const line = document.createElement("div");
      line.className = "line" + (i === 0 ? " fresh" : "");
      line.innerHTML =
        "<span class=\"kind kind-" + kind + "\">" + kind + "</span>" +
        "<span>" + (row.text || "") + "</span>" +
        "<span>" + logAge(row.ms) + "</span>";
      tape.appendChild(line);
    });
  }

  function applyStatus(st) {
    if (!st) return;
    state.applying = true;
    ["hunger", "happy", "energy", "health"].forEach((k) => {
      state.vitals[k] = Number(st[k] !== undefined ? st[k] : st[k === "happy" ? "happy" : k]) || 0;
    });
    if (st.happiness !== undefined && st.happy === undefined) state.vitals.happy = Number(st.happiness) || 0;
    document.querySelectorAll(".bar").forEach((bar) => drawBar(bar, state.vitals[bar.dataset.key]));
    if (!state.previewing) {
      state.lastFace = String(st.face || "idle").toLowerCase();
      state.lastActivity = String(st.activity || st.mood || st.face || "—");
      setFace(state.lastFace, state.lastActivity);
    }
    $("activityHead").textContent = String(st.activity || "—").toUpperCase();
    $("stage").textContent = String(st.stage || "—").toUpperCase();
    $("age").textContent = ageText(st.age);
    $("mood").textContent = String(st.mood || "—").toUpperCase();
    if ($("sysHeap")) $("sysHeap").textContent = heapText(st.heap);
    const air = String(st.air || "CALM").toUpperCase();
    $("airName").textContent = air;
    $("airHead").textContent = air;
    $("airName").style.color = airColor(air);
    $("airHead").style.color = airColor(air);
    $("airCh").textContent = "CH " + String(st.ch ?? "--").padStart(2, "0");
    $("airAps").textContent = st.aps;
    $("airBle").textContent = st.ble;
    $("airRssi").textContent = rssiText(st.rssi);
    $("airRssi").style.color = Number(st.rssi) > -60 ? "var(--good)" : "";
    $("airDeauth").textContent = st.deauth;
    $("airProbe").textContent = st.probe;
    $("airHidden").textContent = st.hidden;
    $("airOpen").textContent = st.open;
    if (st.theme) {
      document.body.className = "theme-" + st.theme;
      setSeg("themeSeg", st.theme);
    }
    if (st.save !== undefined) setSeg("saveSeg", st.save);
    if (st.tft !== undefined) {
      const labels = ["LOW", "MID", "HIGH"];
      const tft = Number.isNaN(Number(st.tft)) ? st.tft : labels[Number(st.tft)] || st.tft;
      setSeg("tftSeg", tft);
    }
    if (st.led !== undefined) {
      const labels = ["LOW", "MID", "HIGH"];
      const led = Number.isNaN(Number(st.led)) ? st.led : labels[Number(st.led)] || st.led;
      setSeg("ledSeg", led);
    }
    setPressed("sleepTog", Number(st.sleep) === 1);
    setPressed("soundTog", Number(st.sound) === 1);
    setPressed("ledsTog", Number(st.leds) === 1);
    if (st.stamps !== undefined) {
      $("stampCount").textContent = st.stamps + "/" + (st.stamps_max || 11);
    }
    state.applying = false;
  }

  function setNeedFw(on, msg) {
    state.needFw = !!on;
    document.body.dataset.needfw = on ? "1" : "0";
    if ($("onboardHint")) $("onboardHint").hidden = !on;
    if ($("plugTitle")) $("plugTitle").textContent = on ? "Face firmware missing" : "Connect your TamaFi";
    if ($("plugSub")) {
      $("plugSub").textContent = on
        ? "Board is on USB but Face is not talking. Install a .bin."
        : "USB cable · close Serial Monitor first";
    }
    if (on) {
      showPage("update");
      if ($("connectHero")) {
        $("connectHero").textContent = "Install firmware";
        $("connectHero").disabled = false;
      }
      note(msg ? String(msg).slice(0, 180) : "Install Face firmware, then Lab will connect.");
    } else if ($("connectHero") && !state.connected) {
      $("connectHero").textContent = "Connect";
    }
  }

  function applyInfo(info) {
    if (!info) return;
    if (info.fw && $("sysFw")) $("sysFw").textContent = String(info.fw);
    if ($("sysProto")) $("sysProto").textContent = info.proto !== undefined ? String(info.proto) : "—";
    if (info.heap !== undefined && $("sysHeap")) $("sysHeap").textContent = heapText(info.heap);
    if (info.minheap !== undefined && $("sysMin")) $("sysMin").textContent = heapText(info.minheap);
    if (info.up_s !== undefined && $("sysUp")) $("sysUp").textContent = upText(info.up_s);
  }

  const Dock = {
    receive(msg) {
      const ev = msg && msg.event;
      const data = msg && msg.data;
      if (ev === "ports") fillPorts(data);
      else if (ev === "connecting") {
        setLink("busy", "Connecting");
        note(data && data.port ? ("Opening " + data.port) : "Connecting");
      } else if (ev === "connected") {
        state.connected = true;
        setNeedFw(false);
        setLink("on", data && data.port ? data.port : "Connected");
        if (data && data.port) {
          if ($("portLabel")) $("portLabel").textContent = data.port;
          const sel = $("port");
          if (sel && [...sel.options].some((o) => o.value === data.port)) sel.value = data.port;
        }
        if ($("fwName") && data && data.ident) $("fwName").textContent = String(data.ident).replace("PONG ", "");
        note(data && data.ident ? data.ident : "linked");
      } else if (ev === "needs_firmware") {
        state.connected = false;
        setLink("off", "Needs Face");
        setNeedFw(true, data && data.msg ? data.msg : "");
      } else if (ev === "disconnected" || ev === "dropped") {
        state.connected = false;
        if (state.needFw) {
          setLink("off", "Needs Face");
          if (data === "flashing") note("Updating firmware…");
        } else {
          setLink("off", "Not connected");
          setFace("sleep", "—");
          note(ev === "dropped" ? ("link lost: " + data) : String(data || "disconnected"));
        }
      } else if (ev === "status") applyStatus(data);
      else if (ev === "info") applyInfo(data);
      else if (ev === "stamps") renderStamps(data);
      else if (ev === "log") renderLog(data);
      else if (ev === "note") note(String(data || "").slice(0, 180));
      else if (ev === "flashlog") {
        $("flashLog").textContent += String(data) + "\n";
        $("flashLog").scrollTop = $("flashLog").scrollHeight;
      } else if (ev === "flashed") {
        $("flashBtn").disabled = !state.hasBin;
        $("flashBtn").textContent = "Update";
        if (data && data.ok === false && state.needFw) {
          note(data.msg ? String(data.msg).slice(0, 180) : "Update failed. Face is still missing.");
        } else {
          note(data && data.msg ? data.msg : "");
        }
      } else if (ev === "error") {
        if (state.needFw && !state.connected) {
          setLink("off", "Needs Face");
        } else {
          setLink(state.connected ? "on" : "off", state.connected ? $("linkText").textContent : "Not connected");
        }
        note("error: " + data);
      }
    },
    async ready() {
      applySnapshot(await call("/api/state"));
    },
  };
  window.Dock = Dock;

  let lastNote = "";
  let lastError = "";
  let lastFlashed = "";

  function applySnapshot(s) {
    if (!s) return;
    if (s.ports) fillPorts(s.ports);
    if (s.has_bin) {
      state.hasBin = true;
      if ($("binName") && s.bin_name) $("binName").textContent = s.bin_name;
      if ($("flashBtn") && !s.flashing) $("flashBtn").disabled = false;
    }
    if (s.flashlog != null && $("flashLog") && $("flashLog").textContent !== s.flashlog) {
      $("flashLog").textContent = s.flashlog;
      $("flashLog").scrollTop = $("flashLog").scrollHeight;
    }
    if (s.flashing) {
      if ($("flashBtn")) {
        $("flashBtn").disabled = true;
        $("flashBtn").textContent = "Updating…";
      }
    }
    if (s.connecting && !s.connected) {
      setLink("busy", "Waiting");
      note(s.note || (s.port ? ("Waiting for Face on " + s.port) : "Waiting for Face"));
    } else if (s.need_fw && s.needs_firmware) {
      Dock.receive({ event: "needs_firmware", data: s.needs_firmware });
    } else if (s.connected) {
      if (!state.connected) {
        Dock.receive({ event: "connected", data: { ident: s.ident, port: s.port } });
      } else {
        setLink("on", s.port || "Connected");
        if (s.port && $("portLabel")) $("portLabel").textContent = s.port;
      }
      if (s.status) applyStatus(s.status);
      if (s.info) applyInfo(s.info);
      if (s.stamps) renderStamps(s.stamps);
      if (s.log) renderLog(s.log);
    } else if (state.connected) {
      Dock.receive({ event: "disconnected", data: "disconnected" });
    } else if (document.body.dataset.link === "busy") {
      setLink("off", "Not connected");
    }
    if (s.flashed) {
      const sig = JSON.stringify(s.flashed);
      if (sig !== lastFlashed) {
        lastFlashed = sig;
        Dock.receive({ event: "flashed", data: s.flashed });
      }
    }
    if (s.error && s.error !== lastError) {
      lastError = s.error;
      Dock.receive({ event: "error", data: s.error });
    } else if (s.note && s.note !== lastNote) {
      lastNote = s.note;
      note(s.note);
    }
  }

  document.querySelectorAll(".tab").forEach((btn) => {
    btn.addEventListener("click", () => showPage(btn.dataset.tab));
  });

  $("refresh").addEventListener("click", async () => {
    fillPorts(await call("/api/ports"));
  });

  $("connect").addEventListener("click", async () => {
    if (state.connected) {
      await call("/api/disconnect", {});
      return;
    }
    const port = $("port").value;
    if (!port) {
      note("No serial port. Plug in TamaFi.");
      return;
    }
    setLink("busy", "Connecting");
    note("Opening " + port);
    await call("/api/connect", { device: port });
  });
  if ($("connectHero")) {
    $("connectHero").addEventListener("click", () => {
      if (state.needFw) {
        showPage("update");
        return;
      }
      $("connect").click();
    });
  }

  if ($("saveNow")) {
    $("saveNow").addEventListener("click", async () => {
      if (!state.connected) {
        note("Connect first.");
        return;
      }
      await call("/api/save", {});
    });
  }

  document.querySelectorAll(".tog").forEach((btn) => {
    btn.addEventListener("click", async () => {
      if (state.applying || !state.connected) return;
      const next = btn.getAttribute("aria-pressed") !== "true";
      setPressed(btn.id, next);
      await call("/api/set", { key: btn.dataset.set, value: next ? 1 : 0 });
    });
  });

  document.querySelectorAll(".seg").forEach((root) => {
    root.addEventListener("click", async (e) => {
      const btn = e.target.closest("button");
      if (!btn || state.applying || !state.connected) return;
      [...root.querySelectorAll("button")].forEach((b) => b.classList.toggle("on", b === btn));
      if (root.dataset.set === "theme") document.body.className = "theme-" + btn.dataset.val;
      await call("/api/set", { key: root.dataset.set, value: btn.dataset.val });
    });
  });

  $("moodGrid").addEventListener("click", async (e) => {
    const btn = e.target.closest("[data-preview]");
    if (!btn) return;
    const code = btn.dataset.preview;
    triggerPreview(code);
    if (!state.connected) return;
    await call("/api/preview", { code: code });
  });

  document.querySelectorAll("[data-key]").forEach((btn) => {
    btn.addEventListener("click", async () => {
      if (!state.connected) return;
      await call("/api/key", { name: btn.dataset.key });
    });
  });

  $("pickBin").addEventListener("click", async () => {
    const res = await call("/api/pick_firmware", {});
    if (res && res.name) {
      state.hasBin = true;
      $("binName").textContent = res.name;
      $("flashBtn").disabled = false;
    }
  });

  $("flashBtn").addEventListener("click", () => {
    const port = $("port").value;
    $("modalTitle").textContent = "Update firmware?";
    $("modalBody").textContent = "This will disconnect TamaFi and write " + port + ". Close Serial Monitor first.";
    $("modal").hidden = false;
    $("modalYes").onclick = async () => {
      $("modal").hidden = true;
      $("flashLog").textContent = "";
      $("flashBtn").disabled = true;
      $("flashBtn").textContent = "Updating…";
      await call("/api/flash", { port: port });
    };
  });
  $("modalNo").addEventListener("click", () => { $("modal").hidden = true; });

  setInterval(async () => {
    applySnapshot(await call("/api/state"));
  }, 400);

  buildPips();
  buildMoodGrid();
  document.querySelectorAll(".bar").forEach((bar) => drawBar(bar, 0));
  renderStamps([]);
  renderLog([]);
  if (window.TamafiFace) window.TamafiFace.attach($("face"));
  setFace("idle", "—");

  async function boot() {
    await Dock.ready();
  }
  window.addEventListener("pywebviewready", boot);
  boot();
  setTimeout(boot, 400);
})();
