// 파이썬(Pyodide)을 화면과 따로 돌리는 작업자. tools/의 코드를 그대로 불러다 쓴다.
// 메시지: {id, cmd, ...} → {id, ok, result | error}, 진행 중 출력은 {log}
const PYODIDE = "https://cdn.jsdelivr.net/pyodide/v0.27.2/full/";
const TOOLS = ["extract_masks", "export_mod_data", "make_sdf", "make_theme_data", "inspect_frames", "hand_masks"];

importScripts(PYODIDE + "pyodide.js");

let py, tool;

async function boot() {
  py = await loadPyodide({ indexURL: PYODIDE });
  const say = (text) => postMessage({ log: text });
  py.setStdout({ batched: say });
  py.setStderr({ batched: say });
  say("loading numpy / scipy / pillow ...");
  await py.loadPackage(["numpy", "scipy", "pillow"]);
  py.FS.mkdirTree("/tools");
  // 캐시 때문에 고친 도구가 반영되지 않는 일이 없도록 매번 새로 받는다
  const bust = "?v=" + Date.now();
  await Promise.all([
    ...TOOLS.map(async (name) => {
      const res = await fetch(new URL(`../tools/${name}.py${bust}`, self.location));
      if (!res.ok) throw new Error(`tools/${name}.py: HTTP ${res.status}`);
      py.FS.writeFile(`/tools/${name}.py`, await res.text());
    }),
    (async () => {
      const res = await fetch(new URL(`webtool.py${bust}`, self.location));
      if (!res.ok) throw new Error(`webtool.py: HTTP ${res.status}`);
      py.FS.writeFile("/tools/webtool.py", await res.text());
    })(),
  ]);
  py.runPython("import sys, importlib; sys.path.insert(0, '/tools'); importlib.invalidate_caches()");
  tool = py.pyimport("webtool");
  say("ready");
}
const ready = boot();

// 파이썬 값 → 그냥 JS 값 (bytes는 Uint8Array로)
function toJs(v) {
  if (v && typeof v.toJs === "function") {
    const out = v.toJs({ dict_converter: Object.fromEntries, create_proxies: false });
    v.destroy();
    return out;
  }
  return v;
}

function writeFiles(root, files) {
  // files: [{path, data(ArrayBuffer), mtime(ms)}]
  for (const f of files) {
    const p = `${root}/${f.path}`;
    py.FS.mkdirTree(p.slice(0, p.lastIndexOf("/")));
    py.FS.writeFile(p, new Uint8Array(f.data));
    if (f.mtime) py.FS.utime(p, f.mtime, f.mtime);
  }
}

function resetDir(root) {
  py.runPython(`import shutil, pathlib; shutil.rmtree(${JSON.stringify(root)}, ignore_errors=True); pathlib.Path(${JSON.stringify(root)}).mkdir(parents=True)`);
}

const commands = {
  // 팩: 폴더에서 고른 파일들 (이미 필요한 것만 골라 보냄) 또는 zip
  packFiles({ files }) { resetDir("/pack"); writeFiles("/pack", files); return tool.pack_info(); },
  packZip({ data }) { return tool.load_pack_zip(new Uint8Array(data)); },
  resFiles({ files }) { resetDir("/res"); writeFiles("/res", files); return tool.pack_info(); },
  // 작업 묶음
  projectFiles({ files }) {
    resetDir("/tmp/project_in");
    writeFiles("/tmp/project_in", files);
    return tool.adopt_project(py.pyimport("pathlib").Path("/tmp/project_in"));
  },
  projectZip({ data }) { return tool.load_project_zip(new Uint8Array(data)); },
  projectJson({ text, name }) { return tool.load_overrides_json(text, name); },
  projectInfo() { return tool.project_info(); },
  projectSave() { return tool.project_zip(); },
  projectClear() { return tool.clear_project(); },
  // 만들기
  build({ profile, wholeFallback, name, packId }) { return tool.build(profile, wholeFallback, name, packId); },
  // 수기 분리
  frames() { return tool.frame_list(); },
  handExport({ frames, profile, wholeFallback }) { return tool.export_hand(py.toPy(frames), profile, wholeFallback); },
  handFiles({ files }) { resetDir("/hand/in"); writeFiles("/hand/in", files); return tool.import_hand(); },
  handZip({ data }) { return tool.import_hand_zip(new Uint8Array(data)); },
};

onmessage = async (e) => {
  const { id, cmd, ...args } = e.data;
  try {
    await ready;
    const t0 = Date.now();
    const raw = await commands[cmd](args);
    const result = toJs(raw);
    if (Date.now() - t0 > 3000) postMessage({ log: `(${cmd}: ${Math.round((Date.now() - t0) / 1000)}s)` });
    const transfer = [];
    // 큰 바이트는 복사 없이 넘긴다
    (function collect(v) {
      if (v instanceof Uint8Array) transfer.push(v.buffer);
      else if (v && typeof v === "object") Object.values(v).forEach(collect);
    })(result);
    postMessage({ id, ok: true, result }, transfer);
  } catch (err) {
    postMessage({ id, ok: false, error: String(err && err.message || err) });
  }
};

ready.then(() => postMessage({ ready: true }), (err) => postMessage({ fatal: String(err && err.message || err) }));
