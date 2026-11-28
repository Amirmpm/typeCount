# Deploying TypeCount landing to Cloudflare Pages

The landing site in `landing/` is fully static — no build step, no dependencies.

## Option A — Git integration (recommended, auto-deploys on push)

1. Cloudflare Dashboard → **Workers & Pages** → **Create** → **Pages** → **Connect to Git**.
2. Pick the `Amirmpm/typeCount` repo.
3. Settings:
   - **Production branch:** `main`
   - **Framework preset:** `None`
   - **Build command:** *(leave empty)*
   - **Build output directory:** `landing`
4. **Save and Deploy.** You get `https://typecount.pages.dev`.

## Option B — CLI (one command)

```bash
npx wrangler pages deploy landing --project-name typecount
```

`wrangler.toml` in the repo root already points at `landing/`.

## Custom domain

1. Pages project → **Custom domains** → **Set up a domain**.
2. If the domain's DNS is on Cloudflare, the `CNAME` record is created automatically.
3. Otherwise, at your registrar add:
   - `www` → `CNAME` → `typecount.pages.dev`
   - apex (`@`) → `CNAME`/`ALIAS` → `typecount.pages.dev`, or the four GitHub-style `A` records only if you're on a provider without ALIAS support — Cloudflare Pages prefers a proxied `CNAME` at the apex.
4. Enable **Always Use HTTPS** (SSL/TLS → Edge Certificates).

## Notes

- The download buttons call `https://api.github.com/repos/Amirmpm/typeCount/releases/latest` from the browser and rewrite their `href` to the newest asset. If the API is rate-limited or offline they fall back to `github.com/Amirmpm/typeCount/releases/latest`, so download links never break.
- Unauthenticated GitHub API calls are limited to 60/hour per IP. To be safe and faster, you can proxy it in a Cloudflare Pages Function — see below.
- `landing/_headers` sets security and caching headers.
- `landing/404.html` is served automatically for unknown paths.

### Optional: cache the GitHub API on Cloudflare

`functions/api/latest-release.js` is included. It caches the response at the edge for 10 minutes and raises the effective rate limit. To use it, swap the fetch URL in `landing/app.js`:

```js
fetch("/api/latest-release")
```

Cloudflare Pages Functions activate automatically when a `functions/` directory exists next to your output directory. Since our output directory is `landing/`, the Functions directory must be at `landing/functions/` — or set the project's build output directory to the repo root and move the site files accordingly. The simplest path is to keep the direct GitHub call (Option A/B above work as-is).
