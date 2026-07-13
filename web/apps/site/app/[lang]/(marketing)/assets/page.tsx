import type { Metadata } from 'next';
import { Card, Section } from '@vortex/ui';
import { getDictionary } from '@/content/dictionary';
import { toLang } from '@/lib/i18n';

interface Props {
  params: Promise<{ lang: string }>;
}

export async function generateMetadata({ params }: Props): Promise<Metadata> {
  const { lang: rawLang } = await params;
  const lang = toLang(rawLang);
  const dict = getDictionary(lang);
  return { title: dict.assets.title, description: dict.assets.description };
}

/**
 * Placeholder for the registry (apps/registry-api + apps/store, not built yet).
 *
 * Deliberately honest: an empty marketplace that pretends to be open destroys trust with the
 * exact audience we need. This page states the plan and nothing more.
 */
export default async function AssetsPage({ params }: Props) {
  const { lang: rawLang } = await params;
  const lang = toLang(rawLang);
  const dict = getDictionary(lang);

  return (
    <Section
      eyebrow={dict.assets.eyebrow}
      title={dict.assets.title}
      description={dict.assets.description}
    >
      <div className="grid gap-5 md:grid-cols-3">
        {dict.assets.plan.map((step) => (
          <Card key={step.phase}>
            <h3 className="font-mono text-sm text-accent-400">{step.phase}</h3>
            <p className="mt-3 text-sm leading-relaxed text-[var(--text-muted)]">{step.body}</p>
          </Card>
        ))}
      </div>
    </Section>
  );
}
