import Link from 'next/link';
import { Badge, ButtonLink, Section } from '@vortex/ui';
import { CodeShowcase } from '@/components/code-showcase';
import { localePath } from '@/components/site-header';
import { getDictionary } from '@/content/dictionary';
import { SNIPPETS } from '@/content/snippets';
import { highlight } from '@/lib/highlight';
import { toLang } from '@/lib/i18n';
import { SITE } from '@/lib/site-config';

export default async function HomePage({ params }: { params: Promise<{ lang: string }> }) {
  const { lang: rawLang } = await params;
  const lang = toLang(rawLang);
  const dict = getDictionary(lang);

  // Highlighting happens on the server; the client only receives HTML.
  const snippets = await Promise.all(
    SNIPPETS.map(async (s) => ({
      id: s.id,
      label: s.label,
      caption: `${s.caption[lang]} — ${s.sourcePath}`,
      html: await highlight(s.code),
    })),
  );

  return (
    <>
      {/* Hero. Left-aligned and quiet: the headline states what the thing is, the code proves it,
          and nothing on the page competes with either. */}
      <div className="relative">
        <div className="bg-grid pointer-events-none absolute inset-0 -z-10" />

        <div className="mx-auto w-full max-w-5xl px-6 pt-20 pb-14">
          <Badge>{dict.hero.badge}</Badge>

          <h1 className="mt-6 max-w-2xl text-4xl font-semibold tracking-tight text-balance sm:text-5xl">
            {dict.hero.titleLead} <span className="text-[var(--accent)]">{dict.hero.titleAccent}</span>.
          </h1>

          <p className="mt-5 max-w-xl text-lg leading-relaxed text-pretty text-[var(--text-muted)]">
            {dict.hero.subtitle}
          </p>

          <div className="mt-8 flex flex-wrap items-center gap-3">
            <ButtonLink href={localePath(lang, '/learn/getting-started')} size="lg">
              {dict.hero.ctaPrimary}
            </ButtonLink>
            <ButtonLink href={SITE.repoUrl} variant="secondary" size="lg" target="_blank" rel="noreferrer">
              {dict.hero.ctaSecondary}
            </ButtonLink>
          </div>

          {/* The install line, styled as what it is: something you paste into a terminal. */}
          <div className="mt-8 inline-flex max-w-full items-center gap-3 overflow-x-auto border border-[var(--border-subtle)] bg-[var(--surface-sunken)] px-4 py-2.5">
            <span className="text-[var(--accent)] select-none">$</span>
            <code className="font-mono text-xs whitespace-nowrap text-[var(--text-muted)]">
              {`git clone ${SITE.repoUrl} && cmake --preset release`}
            </code>
          </div>
        </div>
      </div>

      <Section eyebrow={dict.api.eyebrow} title={dict.api.title} description={dict.api.description}>
        <CodeShowcase snippets={snippets} />
      </Section>

      <Section
        eyebrow={dict.features.eyebrow}
        title={dict.features.title}
        description={dict.features.description}
      >
        <div className="grid gap-px border border-[var(--border-subtle)] bg-[var(--border-subtle)] sm:grid-cols-2 lg:grid-cols-3">
          {/* One shared hairline between cells rather than a border each: the grid reads as a
              single object, and no two borders ever stack into a 2px seam. */}
          {dict.features.items.map((f) => (
            <div key={f.title} className="bg-[var(--surface-raised)] p-6">
              <h3 className="text-sm font-semibold">{f.title}</h3>
              <p className="mt-2 text-sm leading-relaxed text-[var(--text-muted)]">{f.body}</p>
            </div>
          ))}
        </div>
      </Section>

      <Section title={dict.closing.title}>
        <div className="flex flex-wrap items-center gap-3">
          <ButtonLink href={localePath(lang, '/learn')} size="lg">
            {dict.closing.ctaDocs}
          </ButtonLink>
          <ButtonLink href={SITE.repoUrl} variant="secondary" size="lg" target="_blank" rel="noreferrer">
            {dict.closing.ctaGithub}
          </ButtonLink>
        </div>
        <p className="mt-8 text-sm text-[var(--text-muted)]">
          {dict.closing.assetsPrompt}{' '}
          <Link
            href={localePath(lang, '/assets')}
            className="text-[var(--accent)] underline underline-offset-4 hover:no-underline"
          >
            {dict.closing.assetsLink}
          </Link>
          .
        </p>
      </Section>
    </>
  );
}
