# Vortex Web

The website for the Vortex engine: marketing pages, documentation, and blog. Later, the asset
registry.

It is a **fully static site** — no database, no auth, no API server. Everything is prerendered at
build time; the only server-side code is the docs search index (built in-process from the MDX at
compile time) and the i18n proxy that rewrites `/vi/*`. It deploys to a free tier and costs nothing
to run.

## Layout

```
web/
├── apps/
│   └── site/          Next.js 16 — landing, docs, blog
└── packages/
    └── ui/            design tokens + shared React primitives
```

Planned, not built yet: `apps/registry-api` (Hono + Postgres), `packages/registry-sdk`.

## Develop

```bash
pnpm install
pnpm dev            # http://localhost:3000
pnpm build
pnpm typecheck
```

## Internationalisation (en / vi)

English is the default locale and is served **without a prefix** (`/docs`); Vietnamese lives under
`/vi` (`/vi/docs`). English URLs are therefore the canonical ones for search engines.

Two separate mechanisms, on purpose:

- **Docs and blog** are translated as sibling MDX files — `architecture.mdx` → `architecture.vi.mdx`
  (`parser: 'dot'`). A page with no `.vi.mdx` falls back to English instead of vanishing from the
  Vietnamese sidebar.
- **Everything else** (nav, hero, feature cards) lives in
  [`content/dictionary.ts`](apps/site/content/dictionary.ts). `Dictionary` is derived from the
  English entry, so adding a key without translating it is a **type error**, not a silently English
  string.

Components take copy as props; none of them import the dictionary.

To add a language: extend `LANGUAGES` in [`lib/i18n.ts`](apps/site/lib/i18n.ts), add a dictionary,
add the `*.<lang>.mdx` files, and add the locale to the search `localeMap`.

<details>
<summary>Why the search route maps Vietnamese to the English tokenizer</summary>

Orama has no Vietnamese stemmer. Left alone, Fumadocs passes the locale code `vi` to Orama as a
language name, Orama does not recognise it, and the Vietnamese index silently returns `[]` for every
query. Mapping `vi` to the English tokenizer keeps it working — Vietnamese words are space-separated,
so whitespace tokenization is correct; only stemming is lost, which Vietnamese does not need.
</details>

## Conventions

Facts that appear in more than one place (the repository URL, the deployed origin) live in
[`lib/site-config.ts`](apps/site/lib/site-config.ts). Nothing on the site may link to a page that
does not exist — a footer full of 404s is worse than a short footer.

The deployed origin comes from `NEXT_PUBLIC_SITE_URL` (or Vercel's own env var) and falls back to
localhost, so there is no fictional domain baked into the metadata.

## Stack

| Concern | Choice | Why |
| --- | --- | --- |
| Framework | Next.js 16 (App Router) | SSG for docs/SEO, one stack for the eventual registry UI |
| Docs | Fumadocs 16 + MDX | Versioned docs, search, sidebar from the content tree |
| Styling | Tailwind v4 + tokens in `@vortex/ui` | Tokens are the single source of colour/type |
| Highlighting | Shiki | Same themes in MDX and hand-placed snippets |
| Search | Orama (static, in-process) | Zero infra until the docs outgrow it |

### Version constraints worth knowing

- **Next 16 is required.** Fumadocs 16 uses React's `useEffectEvent`, which Next 15's bundled React
  does not export. Downgrading Next breaks the docs UI at build time.
- **fumadocs-core and fumadocs-mdx must be upgraded together.** `fumadocs-mdx` 11.10 emits `files`
  as a function while `fumadocs-core` 15.8 expects an array — the failure mode is an opaque
  `a.map is not a function` at build.
