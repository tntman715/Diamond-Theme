# Web Pack Tool (build theme data without installing anything)

The `web/` folder is a pack tool that runs in the browser. No Python install, no commands.
It runs the Python code in `tools/` as is (via Pyodide), so results are identical to the command-line tools.

> Korean version: [web-tool.ko.md](web-tool.ko.md) · Painting by hand: [hand-masks.en.md](hand-masks.en.md)

## What it does

1. **Add the pack**: drop the pack folder or zip. Only the button sheets are read.
2. **Settings**: pick the pack type (vanilla / glossy / whole, …) and name. Add an existing overrides file if you have one.
3. **Build**: download `theme-data.zip` and unzip it inside the pack folder (a `theme-data` folder next to `GJ_GameSheet03-uhd.png`).
   - **When the pack is updated, rebuild its theme data.** If buttons moved and the old data is still there, the mod notices, leaves that sheet unthemed and shows a "theme data is out of date" notice in game (it never paints masks onto the wrong buttons).
4. **Paint buttons by hand**: export the buttons to fix as a zip. After painting, drop the folder or zip back in; only edited buttons are registered, and "Rebuild" in step 3 applies them.
   - To continue later, use "Save work" in step 2 to download the overrides file and painted masks as a zip.

Pack files are never uploaded; everything is processed inside that browser.
For the Aero pack, whole takes about 15 s and glossy about 45 s (the first run also downloads the engine).

## Publishing it (GitHub Pages, free)

It's a static site that needs no running server: upload the files once and it keeps working.

1. Push this project (the whole `GD-Diamond-Theme` folder) to a GitHub repository.
   - The web tool loads the Python files from `../tools/`, so `web/` and `tools/` must be published together.
2. In the repository's **Settings → Pages**, set Source to "Deploy from a branch", branch `main`, folder `/ (root)`.
3. A few minutes later it opens at `https://<user>.github.io/<repo>/web/`.

- The `.nojekyll` file at the root makes GitHub publish files as they are. Don't delete it.
- Remove local analysis data derived from other people's packs (packs marked `"local": true`, e.g. under `preview/assets`) before publishing.
- The Python engine (Pyodide) is loaded from the jsDelivr CDN, so users need an internet connection.

## Testing on your computer

```bash
python -m http.server 8766 --directory GD-Diamond-Theme
```

Open `http://localhost:8766/web/` in a browser. (Opening it directly via `file://` doesn't work: the worker can't start.)

## Structure

| File | Role |
|---|---|
| `web/index.html` | The page |
| `web/app.js` | File picking / drag and drop, buttons, downloads (Korean/English) |
| `web/worker.js` | Runs Python apart from the page: loads Pyodide and `tools/*.py` |
| `web/webtool.py` | Web glue: virtual folders (`/pack`, `/project`, `/hand`, `/out`), zips, importing painted masks |

- `hand-masks.json` in a work folder exported from the web stores a fingerprint (`maskSha1`) of each exported mask. Import uses it to detect edited buttons, so re-zipping doesn't matter.
- Work folders exported with the command-line `hand_masks.py` can be imported on the web too (judged by file time then).
