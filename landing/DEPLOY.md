# Deploying the TypeCount landing page to Cloudflare

The site in `landing/` is fully static: no build step, no bundler, no runtime
dependencies. It deploys either as a **Worker with static assets** (the setup
below) or as a classic **Pages** project.

---

## The error: "root directory not found"

That happens when the configured **Root directory** does not exist in the branch
Cloudflare clones. Two things commonly cause it:

1. **The branch is wrong.** `landing/` must exist on the branch that Cloudflare
   builds. If landing was only committed to a feature branch, set
   *Settings → Build → Branch control* to that branch, or merge it into `main`.
2. **A leading slash.** Some UIs reject `/landing`; use `landing` without the
   slash.

To confirm from your side:

```bash
git ls-tree -d --name-only origin/main | grep landing
```

If that prints nothing, `landing/` is not on `main` yet.

---

## Correct settings for `npx wrangler deploy`

`npx wrangler deploy` deploys a **Worker**, not a Pages site, so it needs an
`[assets]` block — which is what `landing/wrangler.toml` now contains:

```toml
name = "typecount"
compatibility_date = "2026-11-28"

[assets]
directory = "."
not_found_handling = "404-page"
```

Use these values in the Cloudflare build configuration:

| Field | Value |
| --- | --- |
| Build command | *(none / empty)* |
| Deploy command | `npx wrangler deploy` |
| Root directory | `landing` |
| Build variables | *(none)* |

`directory = "."` resolves relative to the **root directory**, so `landing/` is
published. Verified locally: `wrangler dev` in `landing/` serves `/`, `/style.css`,
`/app.js` and `/live.js` with 200, and returns 404 for `/_headers`, `/DEPLOY.md`
and `/wrangler.toml` thanks to `landing/.assetsignore`.

### Remember to deploy the branch

```bash
git push origin <your-branch>
```

Then either point the Cloudflare project at that branch, or open a PR and merge
into `main`.

---

## Alternative: Cloudflare Pages (recommended for a static site)

Pages is a better fit than a Worker here — free custom domains, preview builds
per commit, and `_headers` support.

1. **Workers & Pages → Create → Pages → Connect to Git.**
2. Framework preset **None**, build command **empty**, output directory
   **`landing`**, production branch **`main`**.
3. Deploy → `https://typecount.pages.dev`.

CLI equivalent:

```bash
npx wrangler pages deploy landing --project-name typecount
```

The root-level `wrangler.toml` was removed on purpose: with a Pages project, a
root `wrangler.toml` containing `pages_build_output_dir` can conflict with the
dashboard settings and with `wrangler deploy`. Keeping exactly one config, inside
`landing/`, avoids that ambiguity.

---

## Custom domain

1. Project → **Custom domains** → **Set up a domain**.
2. If the domain's DNS is on Cloudflare, the CNAME is created automatically.
3. Otherwise at your registrar: `www` → `CNAME` → `typecount.pages.dev` (or the
   Worker's `*.workers.dev` hostname), apex → flattened CNAME/ALIAS.
4. Enable **Always Use HTTPS**.

---

## Headers

`landing/_headers` is honoured by **Pages** only. If you stay on Workers, either
ignore it (the site works fine) or add a tiny Worker entry point that sets the
same headers on the response.

---

## Download links

The buttons call `https://api.github.com/repos/Amirmpm/typeCount/releases/latest`
and rewrite their `href` to the newest asset, matched by filename pattern. If the
API is rate-limited or offline they fall back to
`github.com/Amirmpm/typeCount/releases/latest`, so a download link never breaks.
---

## How the live dashboard actually works

`landing/live.js` is a small, dependency-free counting engine that runs in the browser:

- Listens for real `keydown`, `pointerdown`, `wheel` and `pointermove` events on the page.
- Counts **keystrokes, clicks, scroll gestures and cursor distance** (px → metres at 96 dpi).
- Computes WPM over a sliding 10-second window (`keys / 5` normalised to a minute) and goes to zero after 5 seconds of idle.
- Stores per-day and per-hour buckets in `localStorage` under `typecount.live.v1`, so the 7-day view and the 365-day calendar are your own history. Nothing is sent anywhere.
- Never counts keystrokes inside `password`, `email` or `tel` inputs, and never stores what you type — only counts. The contenteditable practice box is not read or saved.
- Reset and Export buttons clear or download the stored JSON.

A browser fundamentally cannot observe activity in other applications — that requires native OS input taps, which is exactly why the desktop app exists. The page says this explicitly in the "Scope, honestly" note rather than implying otherwise.
