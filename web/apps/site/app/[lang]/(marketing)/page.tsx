import Link from 'next/link';
import { Badge, ButtonLink, Card, Section } from '@vortex/ui';
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
      {/* Hero. The code is the demo: with no runnable engine on the page, the API itself has to
          carry the pitch, so the snippets sit directly under the fold rather than a screen down. */}
      <div className="relative">
        <div className="bg-grid absolute inset-0 -z-10" />
        <div className="mx-auto w-full max-w-3xl px-6 pt-24 pb-16 text-center">
          <Badge>{dict.hero.badge}</Badge>
          <h1 className="mt-6 text-5xl font-semibold tracking-tight text-balance sm:text-6xl">
            {dict.hero.titleLead}{' '}
            <span className="bg-gradient-to-r from-accent-400 to-violet-500 bg-clip-text text-transparent">
              {dict.hero.titleAccent}
            </span>
            .
          </h1>
          <p className="mx-auto mt-6 max-w-xl text-lg text-pretty text-[var(--text-muted)]">
            {dict.hero.subtitle}
          </p>
          <div className="mt-8 flex flex-wrap justify-center gap-3">
            <ButtonLink href={localePath(lang, '/docs/getting-started')} size="lg">
              {dict.hero.ctaPrimary}
            </ButtonLink>
            <ButtonLink href={SITE.repoUrl} variant="secondary" size="lg" target="_blank" rel="noreferrer">
              {dict.hero.ctaSecondary}
            </ButtonLink>
          </div>
          <p className="mt-6 font-mono text-xs text-[var(--text-muted)]">
            git clone {SITE.repoUrl} &amp;&amp; cmake --preset release
          </p>
        </div>

        <div className="mx-auto w-full max-w-4xl px-6 pb-24">
          <CodeShowcase snippets={snippets} />
          <p className="mt-4 text-center text-sm text-[var(--text-muted)]">{dict.api.description}</p>
        </div>
      </div>

      <Section
        eyebrow={dict.features.eyebrow}
        title={dict.features.title}
        description={dict.features.description}
      >
        <div className="grid gap-5 md:grid-cols-2 lg:grid-cols-3">
          {dict.features.items.map((f) => (
            <Card key={f.title}>
              <h3 className="font-semibold">{f.title}</h3>
              <p className="mt-2 text-sm leading-relaxed text-[var(--text-muted)]">{f.body}</p>
            </Card>
          ))}
        </div>
      </Section>

      {/* Closing CTA */}
      <Section title={dict.closing.title} className="text-center">
        <div className="flex flex-wrap justify-center gap-3">
          <ButtonLink href={localePath(lang, '/docs')} size="lg">
            {dict.closing.ctaDocs}
          </ButtonLink>
          <ButtonLink href={SITE.repoUrl} variant="secondary" size="lg" target="_blank" rel="noreferrer">
            {dict.closing.ctaGithub}
          </ButtonLink>
        </div>
        <p className="mt-8 text-sm text-[var(--text-muted)]">
          {dict.closing.assetsPrompt}{' '}
          <Link href={localePath(lang, '/assets')} className="text-accent-400 hover:underline">
            {dict.closing.assetsLink}
          </Link>
          .
        </p>
      </Section>
    </>
  );
}
