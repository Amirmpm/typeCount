# Deploying the TypeCount landing page to Cloudflare

## Caching — why a normal refresh must show the newest version

Two independent mechanisms guarantee this. Either one alone would be enough;
both together means a stale deploy can never be served again.

**1. `Cache-Control` revalidates every request.**

`landing/_headers` sends `public, max-age=0, must-revalidate` for HTML, CSS and
JS. A returning visitor never reuses an hour-old script: the browser asks the
edge whether the file changed. Cloudflare answers `304 Not Modified` from the
file's ETag when nothing changed, so revalidation costs one empty round trip.
Only the two brand images get a long cache.

The original policy had `max-age=3600` on `/style.css` and `/app.js`, which is
exactly why a hard refresh was needed after a deploy.

**2. Every deploy writes new asset URLs.**

`scripts/assets-version.mjs` appends `?v=<content hash>` to the CSS/JS URLs in
`landing/index.html`:

```html
<link rel="stylesheet" href="/style.css?v=faa5c7487b">
<script src="/live.js?v=98f04442bf"></script>
<script src="/app.js?v=8f9aee5e45"></script>
```

Because the query string is derived from the file contents, a changed file
always gets a new URL — so even a browser holding a previously-cached long
`max-age` copy cannot serve the old code.

### Before every deploy

```bash
npm run version:assets    # rewrite the ?v= hashes
```

Or let the deploy script do it for you:

```bash
npm run deploy            # version + wrangler deploy
npm run deploy:pages      # version + pages deploy
```

`npm run version:check` exits non-zero if the hashes are stale, which is the
right thing to wire into CI. The script is idempotent, so running it twice is
harmless.

Heads-up on Cloudflare's header semantics: a nested `_headers` rule that sets
`Cache-Control` is *joined* with the `/*` rule rather than replacing it, and
RFC 9111 then resolves the duplicate to the **first** value. The file therefore
uses the documented `! Cache-Control` detach line before any nested
`Cache-Control`, which is why the images really do get their long cache.

Verified with `wrangler dev`:

```
/                 public, max-age=0, must-revalidate
/style.css        public, max-age=0, must-revalidate
/live.js          public, max-age=0, must-revalidate
/app.js           public, max-age=0, must-revalidate
/assets/icon.svg  public, max-age=604800, stale-while-revalidate=86400
/robots.txt       public, max-age=3600
```


## Does this site use the 100,000 requests/day quota? — No.

This project is **pure static assets**: HTML, CSS, JS and two images. There is
no Worker code (`main` is not defined in `wrangler.toml`), no Pages Functions,
no `functions/` directory.

Cloudflare's own documentation and pricing state that **requests to static
assets are free and unlimited and do not count toward the Workers/Pages
Functions 100,000-per-day request limit**. Only invocations that execute Worker
code are metered. Because this Worker has no script, the only thing served is
static files — so the daily request quota is untouched even at high traffic.

Additional safeguards already in place:

| Concern | Status |
| --- | --- |
| Worker invocations | None — no `main` entry point |
| Pages Functions | None — no `functions/` directory |
| KV / D1 / R2 / Durable Objects | Not used |
| `wrangler.toml` assets-only | Yes — `[assets]` block without `main` |
| Build minutes (Pages) | No build step; ~0 build time |
| Client-side API calls | 1 per visitor per 6h, cached in `sessionStorage` |

The one outbound request is the GitHub Releases API call used to resolve the
newest download links. It is made **from the visitor's browser to
api.github.com** — it never touches Cloudflare — and the result is cached in
`sessionStorage` for 6 hours so repeat views make no request at all. If GitHub
rate-limits the caller, every button falls back to
`github.com/Amirmpm/typeCount/releases/latest`, which still works.

**What does count on Cloudflare:** deployments (not requests). Workers Free
allows 100 deploy operations per day; Pages Free allows 500 builds per month.
You would need to deploy constantly to approach either.

---

## The error: "root directory not found"

That happens when the configured **Root directory** does not exist in the branch
Cloudflare clones. Two things commonly cause it:

1. **The branch is wrong.** `landing/` must exist on the branch that Cloudflare
   builds. If landing was only committed to a feature branch, merge it into
   `main` or point the project at that branch.
2. **A leading slash.** Some UIs reject `/landing`; use `landing`.

Confirm from your side:

```bash
git ls-tree -d --name-only origin/main | grep landing
```

---

## Deployment settings

If you deploy with `npx wrangler deploy`, wrangler deploys a **Worker with
static assets** (not a Pages site), which needs the `[assets]` block now in
`landing/wrangler.toml`:

```toml
name = "typecount"
compatibility_date = "2026-11-28"

[assets]
directory = "."
not_found_handling = "404-page"
```

| Field | Value |
| --- | --- |
| Build command | *(none / empty)* |
| Deploy command | `npx wrangler deploy` |
| Root directory | `landing` |
| Build variables | *(none)* |

`directory = "."` resolves relative to the root directory, so `landing/` is
published. `landing/.assetsignore` keeps `wrangler.toml`, `DEPLOY.md` and
`_headers` out of the published site.

Verified locally with `wrangler dev` inside `landing/`: `/`, `/style.css`,
`/app.js`, `/live.js` → 200; `/_headers`, `/DEPLOY.md`, `/wrangler.toml` → 404.

---

## Alternative: Cloudflare Pages

| Field | Value |
| --- | --- |
| Framework preset | None |
| Build command | *(empty)* |
| Build output directory | `landing` |
| Production branch | `main` |

CLI equivalent:

```bash
npx wrangler pages deploy landing --project-name typecount
```

There is deliberately no root-level `wrangler.toml`: with a Pages project it can
conflict with the dashboard settings. Exactly one config exists, inside
`landing/`.

---

## Custom domain

1. Project → **Custom domains** → **Set up a domain**.
2. If DNS is on Cloudflare the CNAME is created automatically.
3. Otherwise at your registrar: `www` → `CNAME` → `typecount.pages.dev` (or the
   Worker's `*.workers.dev` host), apex → flattened CNAME/ALIAS.
4. Enable **Always Use HTTPS**.

---

## Download links

Buttons resolve to the newest GitHub release by matching asset filenames. The
result is cached in `sessionStorage` for six hours; on failure they fall back to
`/releases/latest`, so a link never breaks.

---

## How the live dashboard works

`landing/live.js` is a dependency-free counting engine:

- Listens for real `keydown`, `pointerdown`, `wheel` and `pointermove` events.
- Counts keystrokes, clicks, scroll gestures and cursor distance (px → metres at
  96 dpi).
- WPM uses a sliding 10-second window (`keys / 5` normalised to a minute) and
  drops to zero after 5 seconds of idle.
- Stores per-day and per-hour buckets in `localStorage` under
  `typecount.live.v1`, so the 7-day view and the 365-day calendar are your own
  history. Nothing is uploaded.
- Never counts keystrokes inside `password`, `email` or `tel` inputs, and never
  stores what you type — only counts. The practice box's text is not read.
- Reset clears the store; Export downloads it as JSON.

A browser cannot observe activity in other applications — that requires native
OS input taps, which is why the desktop app exists. The page states this
explicitly in its "Scope, honestly" note.
