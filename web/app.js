// Diamond Theme 팩 도구: 화면 쪽. 계산은 worker.js(파이썬)가 한다. 파일은 이 컴퓨터 밖으로 나가지 않는다.
"use strict";

// ---------------------------------------------------------------- 글자 (한국어 / 영어)
const TEXT = {
  ko: {
    subtitle: "텍스쳐 팩에 넣을 테마 데이터를 만듭니다. 설치 없이 브라우저 안에서 처리합니다.",
    engineLoading: "준비 중…", engineReady: "준비됨", engineFail: "불러오기 실패",
    packTitle: "팩 넣기",
    packHint: "텍스쳐 팩 폴더나 zip을 넣으세요. 버튼 시트(GJ_GameSheet03/04, GJ_button, GJ_square 등)만 읽습니다.",
    dropPack: "여기에 팩 폴더나 zip을 끌어다 놓으세요",
    pickFolder: "폴더 고르기", pickZip: "zip 고르기", pickZipJson: "zip / json 고르기",
    resHint: "이 팩에는 시트 배치 정보(.plist)가 없는 시트가 있습니다. 게임의 Resources 폴더를 골라 주세요 (예: Steam\\steamapps\\common\\Geometry Dash\\Resources). plist 파일만 읽습니다.",
    pickRes: "GD Resources 폴더 고르기",
    settingsTitle: "설정",
    profile: "팩 종류",
    pVanilla: "vanilla: 검은 선으로 나뉜 팩 (바닐라, 색만 바꾼 팩)",
    pGlossy: "glossy: 검은 선 없는 광택/렌더링 팩",
    pWhole: "whole: 나누지 않음 (복잡한 팩, 가장 안전)",
    pGeneric: "generic: 어두운 바탕 위 색/흰 요소 (Diamond Vortex 등)",
    pDiamond: "diamond: Diamond Pack 전용",
    wholeFallback: "나누지 못한 버튼은 한 덩어리로라도 처리 (--whole-fallback)",
    packName: "팩 이름",
    projectTitle: "보정 파일 / 칠한 마스크 (선택)",
    projectHint: "이미 만든 보정 파일(.json)과 칠한 마스크 폴더를 함께 넣으면 그대로 반영됩니다. 4단계에서 칠한 마스크를 가져오면 여기에 쌓입니다. 다음에 이어서 하려면 '작업 저장'으로 받아 두세요.",
    dropProject: "보정 파일(.json)이 든 폴더나 zip, 또는 .json 하나를 끌어다 놓으세요",
    saveProject: "작업 저장 (zip)", clearProject: "비우기",
    projectNone: "보정 없음 (자동 분리 그대로)",
    projectSummary: (p) => `${p.file}: 칠한 마스크 ${p.masks}개, 영역 보정 ${p.regions}개, 한 덩어리 지정 ${p.whole}개` +
      (p.themed || p.plain ? `, 강제 켜기/끄기 ${p.themed}/${p.plain}` : ""),
    missingMasks: (n) => `칠한 마스크 파일이 없는 항목 ${n}개 (폴더째 넣었는지 확인하세요)`,
    wholeIgnores: "whole 팩은 모든 버튼이 한 덩어리라 칠한 마스크/영역 보정이 무시됩니다. 칠한 버튼을 쓰려면 vanilla나 glossy를 고르세요.",
    buildTitle: "테마 데이터 만들기",
    buildHint: "팩 크기에 따라 1~5분쯤 걸립니다. 그동안 이 탭을 닫지 마세요.",
    build: "만들기", rebuild: "다시 만들기 (칠한 마스크 반영)",
    building: "만드는 중…",
    built: (n, s) => `완료: 시트 ${n}개, 단독 이미지 ${s}개`,
    download: "theme-data.zip 받기",
    install1: "받은 zip을 팩 폴더 안에 풀어 GJ_GameSheet03-uhd.png 옆에 theme-data 폴더가 생기게 합니다.",
    install2: "Texture Loader에서 팩을 켜면 Diamond Theme 모드가 자동으로 읽습니다.",
    handTitle: "버튼 직접 칠하기 (수기 분리)",
    handHint: "자동 분리가 잘못된 버튼만 골라 꺼낸 뒤, 그림 프로그램에서 빨강(테두리)/초록(바탕)/파랑(아이콘)으로 칠해 다시 넣습니다.",
    filter: "버튼 이름 찾기 (예: playBtn)", buttonsOnly: "버튼만", selectShown: "보이는 것 모두 선택", selectNone: "선택 해제",
    needPack: "먼저 1단계에서 팩을 넣으세요.",
    handExport: "고른 버튼 꺼내기 (zip)",
    selected: (n) => `${n}개 선택됨`,
    hand1: "받은 zip을 풀고, 버튼 폴더마다 mask.png(또는 .ora 파일의 mask 레이어)를 칠해서 저장합니다. 자세한 방법은 zip 안 README.md에 있습니다.",
    hand2: "칠한 작업 폴더(또는 다시 묶은 zip)를 아래에 넣으면 고친 버튼만 등록됩니다.",
    hand3: "3단계 '다시 만들기'로 테마 데이터를 다시 받습니다.",
    dropHand: "칠한 작업 폴더나 zip을 끌어다 놓으세요",
    handResult: (d, u, p) => `가져옴 ${d}개, 안 고친 버튼 ${u}개` + (p ? `, 문제 ${p}개` : ""),
    exported: (n) => `${n}개 버튼을 꺼냈습니다.`,
    logTitle: "진행 기록",
    privacy: "팩 파일은 서버로 올라가지 않고 이 브라우저 안에서만 처리됩니다.",
    packLoaded: (n) => `이미지 ${n}개를 읽었습니다.`,
    noSheets: "버튼 시트(GJ_GameSheet03-uhd.png 등)를 찾지 못했습니다. UHD 팩인지 확인하세요.",
    missingPlists: (l) => `배치 정보(.plist)가 없는 시트: ${l}`,
    busy: "처리 중…",
    error: "오류",
  },
  en: {
    subtitle: "Builds theme data for a texture pack. Runs in your browser, nothing to install.",
    engineLoading: "Loading…", engineReady: "Ready", engineFail: "Failed to load",
    packTitle: "Add the pack",
    packHint: "Add the texture pack folder or zip. Only the button sheets (GJ_GameSheet03/04, GJ_button, GJ_square, …) are read.",
    dropPack: "Drop the pack folder or zip here",
    pickFolder: "Choose folder", pickZip: "Choose zip", pickZipJson: "Choose zip / json",
    resHint: "Some sheets in this pack have no layout file (.plist). Choose the game's Resources folder (e.g. Steam\\steamapps\\common\\Geometry Dash\\Resources). Only plist files are read.",
    pickRes: "Choose GD Resources folder",
    settingsTitle: "Settings",
    profile: "Pack type",
    pVanilla: "vanilla: split by black lines (vanilla, recolored packs)",
    pGlossy: "glossy: glossy/rendered packs without black lines",
    pWhole: "whole: no split (complex packs, safest)",
    pGeneric: "generic: color/white parts on a dark plate (Diamond Vortex, …)",
    pDiamond: "diamond: Diamond Pack only",
    wholeFallback: "Keep buttons that can't be split in one piece (--whole-fallback)",
    packName: "Pack name",
    projectTitle: "Overrides / painted masks (optional)",
    projectHint: "Add an existing overrides file (.json) together with its painted mask folder to apply it. Masks imported in step 4 are added here. Use 'Save work' to continue later.",
    dropProject: "Drop a folder or zip containing the overrides (.json), or a single .json",
    saveProject: "Save work (zip)", clearProject: "Clear",
    projectNone: "No overrides (automatic split as is)",
    projectSummary: (p) => `${p.file}: ${p.masks} painted masks, ${p.regions} region fixes, ${p.whole} set to one piece` +
      (p.themed || p.plain ? `, forced on/off ${p.themed}/${p.plain}` : ""),
    missingMasks: (n) => `${n} entries point to missing mask files (add the whole folder)`,
    wholeIgnores: "A whole pack keeps every button in one piece, so painted masks / region fixes are ignored. Choose vanilla or glossy to use painted buttons.",
    buildTitle: "Build theme data",
    buildHint: "Takes about 1–5 minutes depending on the pack. Keep this tab open.",
    build: "Build", rebuild: "Rebuild (with painted masks)",
    building: "Building…",
    built: (n, s) => `Done: ${n} sheets, ${s} single images`,
    download: "Download theme-data.zip",
    install1: "Unzip it inside the pack folder so a theme-data folder sits next to GJ_GameSheet03-uhd.png.",
    install2: "Enable the pack in Texture Loader; the Diamond Theme mod reads it automatically.",
    handTitle: "Paint buttons by hand",
    handHint: "Export only the buttons that split wrong, paint them red (outline) / green (body) / blue (icon) in an image editor, and import them back.",
    filter: "Find a button (e.g. playBtn)", buttonsOnly: "Buttons only", selectShown: "Select shown", selectNone: "Select none",
    needPack: "Add a pack in step 1 first.",
    handExport: "Export selected buttons (zip)",
    selected: (n) => `${n} selected`,
    hand1: "Unzip it and paint mask.png (or the mask layer of the .ora file) in each button folder. Details are in README.md inside the zip.",
    hand2: "Drop the painted work folder (or a zip of it) below; only edited buttons are imported.",
    hand3: "Use 'Rebuild' in step 3 to download the theme data again.",
    dropHand: "Drop the painted work folder or zip here",
    handResult: (d, u, p) => `${d} imported, ${u} unchanged` + (p ? `, ${p} problems` : ""),
    exported: (n) => `Exported ${n} buttons.`,
    logTitle: "Log",
    privacy: "Pack files are never uploaded; everything is processed inside this browser.",
    packLoaded: (n) => `Read ${n} images.`,
    noSheets: "No button sheets (GJ_GameSheet03-uhd.png, …) found. Make sure it is a UHD pack.",
    missingPlists: (l) => `Sheets without a layout file (.plist): ${l}`,
    busy: "Working…",
    error: "Error",
  },
};
let lang = "ko";
try { lang = localStorage.getItem("dt-lang") || (navigator.language.startsWith("ko") ? "ko" : "en"); } catch { /* 저장소 없음 */ }
const t = (key, ...a) => { const v = TEXT[lang][key] ?? TEXT.ko[key] ?? key; return typeof v === "function" ? v(...a) : v; };
const $ = (id) => document.getElementById(id);

function applyLang() {
  document.documentElement.lang = lang;
  $("lang").value = lang;
  document.querySelectorAll("[data-i18n]").forEach((el) => { el.textContent = t(el.dataset.i18n); });
  document.querySelectorAll("[data-i18n-ph]").forEach((el) => { el.placeholder = t(el.dataset.i18nPh); });
  $("engine").textContent = t(state.engine === "ready" ? "engineReady" : state.engine === "fail" ? "engineFail" : "engineLoading");
  refresh();
}

// ---------------------------------------------------------------- 상태
const state = {
  engine: "loading", busy: false,
  pack: null,          // {files, missingPlists}
  project: null,       // project_info()
  frames: [],          // [{name, button}]
  selected: new Set(),
  built: null,         // {zip, sheets, singles, masks}
  dirty: false,        // 마지막으로 만든 뒤 보정이 바뀌었는지
  slug: "pack",
};

// ---------------------------------------------------------------- 작업자
const worker = new Worker("worker.js");
let seq = 0;
const pending = new Map();
const log = (text) => { const el = $("log"); el.textContent += text + "\n"; el.scrollTop = el.scrollHeight; };
worker.onmessage = (e) => {
  const m = e.data;
  if (m.log !== undefined) return log(m.log);
  if (m.ready) { state.engine = "ready"; $("engine").className = "ok"; $("engine").textContent = t("engineReady"); refreshProject(); return refresh(); }
  if (m.fatal) { state.engine = "fail"; $("engine").className = "bad"; $("engine").textContent = t("engineFail"); log("! " + m.fatal); return; }
  const p = pending.get(m.id);
  if (!p) return;
  pending.delete(m.id);
  m.ok ? p.resolve(m.result) : p.reject(new Error(m.error));
};
function call(cmd, args = {}, transfer = []) {
  return new Promise((resolve, reject) => {
    const id = ++seq;
    pending.set(id, { resolve, reject });
    worker.postMessage({ ...args, cmd, id }, transfer);
  });
}

// 바쁜 동안 버튼을 막고, 결과/오류를 상태 칸에 보여 준다
async function run(statusEl, fn) {
  if (state.busy) return;
  state.busy = true;
  refresh();
  statusEl.innerHTML = `<span class="spin"></span>${t("busy")}`;
  try {
    await fn();
  } catch (err) {
    statusEl.innerHTML = `<span class="bad">${t("error")}: ${escapeHtml(err.message)}</span>`;
    log("! " + err.message);
  } finally {
    state.busy = false;
    refresh();
  }
}
const escapeHtml = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));

// ---------------------------------------------------------------- 파일 모으기 (고르기 / 끌어다 놓기)
const NEEDED = /^(GJ_GameSheet03|GJ_GameSheet04|GJ_LaunchSheet|GJ_button_0\d|GJ_square0\d)-uhd\.(png|plist|fnt)$/;

// FileList(폴더 고르기) → [{path, file}]
const fromInput = (input) => [...input.files].map((file) => ({ path: file.webkitRelativePath || file.name, file }));

// 끌어다 놓은 것 → [{path, file}] (폴더는 안까지 훑는다)
async function fromDrop(dt) {
  const entries = [...dt.items].map((i) => i.webkitGetAsEntry && i.webkitGetAsEntry()).filter(Boolean);
  if (!entries.length) return [...dt.files].map((file) => ({ path: file.name, file }));
  const out = [];
  async function walk(entry, prefix) {
    if (entry.isFile) {
      const file = await new Promise((res, rej) => entry.file(res, rej));
      out.push({ path: prefix + entry.name, file });
    } else if (entry.isDirectory) {
      const reader = entry.createReader();
      let batch;
      do {
        batch = await new Promise((res, rej) => reader.readEntries(res, rej));
        for (const child of batch) await walk(child, prefix + entry.name + "/");
      } while (batch.length);
    }
  }
  for (const e of entries) await walk(e, "");
  return out;
}

async function pack(items, mapPath = (p) => p) {
  const files = [];
  for (const { path, file } of items) {
    files.push({ path: mapPath(path), data: await file.arrayBuffer(), mtime: file.lastModified });
  }
  return files;
}
const transferOf = (files) => files.map((f) => f.data);
const baseName = (p) => p.split("/").pop();
const isZip = (items) => items.length === 1 && /\.zip$/i.test(items[0].path);

function hookDrop(zone, handler) {
  zone.addEventListener("dragover", (e) => { e.preventDefault(); zone.classList.add("over"); });
  zone.addEventListener("dragleave", () => zone.classList.remove("over"));
  zone.addEventListener("drop", async (e) => {
    e.preventDefault();
    zone.classList.remove("over");
    handler(await fromDrop(e.dataTransfer));
  });
}
function hookPick(btnId, inputId, handler) {
  $(btnId).onclick = () => $(inputId).click();
  $(inputId).onchange = () => { const items = fromInput($(inputId)); $(inputId).value = ""; if (items.length) handler(items); };
}

// ---------------------------------------------------------------- 1. 팩
function slugify(s) { return s.toLowerCase().replace(/\.zip$/i, "").replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "") || "pack"; }

async function loadPack(items) {
  await run($("packStatus"), async () => {
    let info;
    const top = items[0].path.split("/")[0];
    if (isZip(items)) {
      const data = await items[0].file.arrayBuffer();
      info = await call("packZip", { data }, [data]);
      setName(items[0].path.replace(/\.zip$/i, ""));
    } else {
      // 같은 이름이 여러 폴더에 있으면 가장 얕은 것 (팩 안의 대체 버전/예비 폴더 무시)
      const best = new Map();
      for (const it of items) {
        const name = baseName(it.path);
        if (!NEEDED.test(name)) continue;
        const prev = best.get(name);
        if (!prev || it.path.split("/").length < prev.path.split("/").length) best.set(name, it);
      }
      const files = await pack([...best.values()], baseName);
      info = await call("packFiles", { files }, transferOf(files));
      setName(top);
    }
    showPack(info);
    state.built = null;
    state.frames = await call("frames");
    state.selected.clear();
    renderFrames();
  });
}

function setName(name) {
  // 팩 폴더 이름이 "팩 이름 (2)" 처럼 생겼으면 그대로 둔다. 사용자가 이미 적었으면 건드리지 않는다
  if (!$("packName").value || $("packName").dataset.auto === "1") {
    $("packName").value = name;
    $("packName").dataset.auto = "1";
  }
}

function showPack(info) {
  state.pack = info;
  const sheets = info.files.length;
  let html = sheets ? `<span class="ok">${t("packLoaded", sheets)}</span>` : `<span class="bad">${t("noSheets")}</span>`;
  if (info.missingPlists.length) html += `<br><span class="warn">${escapeHtml(t("missingPlists", info.missingPlists.join(", ")))}</span>`;
  $("packStatus").innerHTML = html;
  $("resBox").classList.toggle("hidden", !info.missingPlists.length);
}

async function loadRes(items) {
  await run($("packStatus"), async () => {
    const plists = items.filter((it) => /-uhd\.plist$/.test(it.path) && NEEDED.test(baseName(it.path)));
    const files = await pack(plists, baseName);
    showPack(await call("resFiles", { files }, transferOf(files)));
    state.frames = await call("frames");
    renderFrames();
  });
}

// ---------------------------------------------------------------- 2. 작업 묶음
async function loadProject(items) {
  await run($("projectStatus"), async () => {
    let info;
    if (isZip(items)) {
      const data = await items[0].file.arrayBuffer();
      info = await call("projectZip", { data }, [data]);
    } else if (items.length === 1 && /\.json$/i.test(items[0].path)) {
      info = await call("projectJson", { text: await items[0].file.text(), name: baseName(items[0].path) });
    } else {
      const files = await pack(items.filter((it) => /\.(json|png)$/i.test(it.path)));
      info = await call("projectFiles", { files }, transferOf(files));
    }
    showProject(info);
    state.dirty = true;
  });
}

function showProject(info) {
  state.project = info;
  const empty = !info.masks && !info.regions && !info.whole && !info.themed && !info.plain;
  let html = empty ? t("projectNone") : `<span class="ok">${escapeHtml(t("projectSummary", info))}</span>`;
  if (info.missingMasks.length) html += `<br><span class="warn">${escapeHtml(t("missingMasks", info.missingMasks.length))}</span>`;
  if ($("profile").value === "whole" && (info.masks || info.regions)) html += `<br><span class="warn">${t("wholeIgnores")}</span>`;
  $("projectStatus").innerHTML = html;
}
async function refreshProject() { try { showProject(await call("projectInfo")); } catch (e) { log("! " + e.message); } }

function download(bytes, name) {
  const url = URL.createObjectURL(new Blob([bytes], { type: "application/zip" }));
  const a = document.createElement("a");
  a.href = url;
  a.download = name;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 60000);
}

// ---------------------------------------------------------------- 3. 만들기
async function build() {
  await run($("buildStatus"), async () => {
    const name = $("packName").value.trim() || "Pack";
    state.slug = slugify(name);
    log(`--- build: ${name} (${$("profile").value}) ---`);
    $("buildStatus").innerHTML = `<span class="spin"></span>${t("building")}`;
    const res = await call("build", { profile: $("profile").value, wholeFallback: $("wholeFallback").checked, name, packId: state.slug });
    state.built = res;
    state.dirty = false;
    $("buildStatus").innerHTML = `<span class="ok">${t("built", res.sheets.length, res.singles.length)}</span>`;
    const thumbs = $("maskThumbs");
    thumbs.querySelectorAll("img").forEach((img) => URL.revokeObjectURL(img.src));
    thumbs.innerHTML = "";
    for (const [file, bytes] of Object.entries(res.masks)) {
      const fig = document.createElement("figure");
      const img = document.createElement("img");
      img.src = URL.createObjectURL(new Blob([bytes], { type: "image/png" }));
      img.alt = file;
      img.onclick = () => window.open(img.src, "_blank");
      fig.append(img);
      const cap = document.createElement("figcaption");
      cap.textContent = file;
      fig.append(cap);
      thumbs.append(fig);
    }
  });
}

// ---------------------------------------------------------------- 4. 수기 분리
function shownFrames() {
  const q = $("frameFilter").value.trim().toLowerCase();
  const only = $("buttonsOnly").checked;
  return state.frames.filter((f) => (!only || f.button) && (!q || f.name.toLowerCase().includes(q)));
}

function renderFrames() {
  const list = $("frameList");
  if (!state.frames.length) {
    list.innerHTML = `<span class="hint">${t("needPack")}</span>`;
    return refresh();
  }
  const shown = shownFrames();
  list.innerHTML = "";
  const frag = document.createDocumentFragment();
  for (const f of shown) {
    const label = document.createElement("label");
    label.title = f.name;
    const box = document.createElement("input");
    box.type = "checkbox";
    box.checked = state.selected.has(f.name);
    box.onchange = () => { box.checked ? state.selected.add(f.name) : state.selected.delete(f.name); refresh(); };
    label.append(box, document.createTextNode(f.name));
    frag.append(label);
  }
  list.append(frag);
  refresh();
}

async function handExport() {
  await run($("handStatus"), async () => {
    const res = await call("handExport", { frames: [...state.selected], profile: $("profile").value, wholeFallback: $("wholeFallback").checked });
    download(res.zip, `${state.slug || "pack"}-hand-work.zip`);
    $("handStatus").innerHTML = `<span class="ok">${t("exported", res.count)}</span>`;
  });
}

async function handImport(items) {
  await run($("handStatus"), async () => {
    let res;
    if (isZip(items)) {
      const data = await items[0].file.arrayBuffer();
      res = await call("handZip", { data }, [data]);
    } else {
      const files = await pack(items.filter((it) => /\.(png|ora|json)$/i.test(it.path)));
      res = await call("handFiles", { files }, transferOf(files));
    }
    let html = `<span class="${res.done.length ? "ok" : "warn"}">${t("handResult", res.done.length, res.unchanged.length, res.problems.length)}</span>`;
    for (const p of res.problems) html += `<br><span class="warn">${escapeHtml(p)}</span>`;
    $("handStatus").innerHTML = html;
    const box = $("handChecks");
    box.querySelectorAll("img").forEach((img) => URL.revokeObjectURL(img.src));
    box.innerHTML = "";
    for (const c of res.checks) {
      const fig = document.createElement("figure");
      const img = document.createElement("img");
      img.src = URL.createObjectURL(new Blob([c.png], { type: "image/png" }));
      const cap = document.createElement("figcaption");
      cap.textContent = `${c.name} (${c.file})`;
      fig.append(cap, img);
      box.append(fig);
    }
    showProject(res.project);
    if (res.done.length) state.dirty = true;
  });
}

// ---------------------------------------------------------------- 화면 갱신
function refresh() {
  const ready = state.engine === "ready" && !state.busy;
  const hasPack = !!(state.pack && state.pack.files.length);
  $("buildBtn").disabled = !ready || !hasPack;
  $("buildBtn").textContent = t(state.built && state.dirty ? "rebuild" : "build");
  $("buildResult").classList.toggle("hidden", !state.built);
  $("handExportBtn").disabled = !ready || !hasPack || !state.selected.size;
  $("selCount").textContent = state.selected.size ? t("selected", state.selected.size) : "";
  for (const id of ["packFolderBtn", "packZipBtn", "resBtn", "projectFolderBtn", "projectFileBtn", "projectSaveBtn", "projectClearBtn", "handFolderBtn", "handZipBtn"]) {
    $(id).disabled = !ready;
  }
}

// ---------------------------------------------------------------- 연결
hookDrop($("packDrop"), (items) => items.length && loadPack(items));
hookPick("packFolderBtn", "packFolder", loadPack);
hookPick("packZipBtn", "packZip", loadPack);
hookPick("resBtn", "resFolder", loadRes);
hookDrop($("projectDrop"), (items) => items.length && loadProject(items));
hookPick("projectFolderBtn", "projectFolder", loadProject);
hookPick("projectFileBtn", "projectFile", loadProject);
$("projectSaveBtn").onclick = () => run($("projectStatus"), async () => {
  download(await call("projectSave"), `${state.slug || "pack"}-overrides.zip`);
  await refreshProject();
});
$("projectClearBtn").onclick = () => run($("projectStatus"), async () => { showProject(await call("projectClear")); state.dirty = true; });
$("buildBtn").onclick = build;
$("downloadBtn").onclick = () => state.built && download(state.built.zip, `${state.slug}-theme-data.zip`);
$("frameFilter").oninput = renderFrames;
$("buttonsOnly").onchange = renderFrames;
$("selAll").onclick = () => { shownFrames().forEach((f) => state.selected.add(f.name)); renderFrames(); };
$("selNone").onclick = () => { state.selected.clear(); renderFrames(); };
$("handExportBtn").onclick = handExport;
hookDrop($("handDrop"), (items) => items.length && handImport(items));
hookPick("handFolderBtn", "handFolder", handImport);
hookPick("handZipBtn", "handZip", handImport);
$("packName").oninput = () => { $("packName").dataset.auto = "0"; };
$("profile").onchange = () => { if (state.project) showProject(state.project); state.dirty = true; refresh(); };
$("wholeFallback").onchange = () => { state.dirty = true; refresh(); };
$("lang").onchange = () => { lang = $("lang").value; try { localStorage.setItem("dt-lang", lang); } catch { /* 저장소 없음 */ } applyLang(); renderFrames(); };
window.addEventListener("dragover", (e) => e.preventDefault());
window.addEventListener("drop", (e) => e.preventDefault());

applyLang();
