import type { ReactNode } from 'react';
import { SiteFooter } from '@/components/site-footer';
import { SiteHeader } from '@/components/site-header';
import { getDictionary } from '@/content/dictionary';
import { toLang } from '@/lib/i18n';

/**
 * Marketing shell. Docs live under /docs with the fumadocs layout instead, which is why this
 * is a route group rather than something in the root layout.
 */
export default async function MarketingLayout({
  children,
  params,
}: {
  children: ReactNode;
  params: Promise<{ lang: string }>;
}) {
  const { lang: rawLang } = await params;
  const lang = toLang(rawLang);
  const dict = getDictionary(lang);

  return (
    <>
      <SiteHeader lang={lang} dict={dict} />
      <main className="flex-1">{children}</main>
      <SiteFooter lang={lang} dict={dict} />
    </>
  );
}
