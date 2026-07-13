import type { Metadata } from 'next';
import { notFound } from 'next/navigation';
import { RootProvider } from 'fumadocs-ui/provider/next';
import type { ReactNode } from 'react';
import { LANGUAGES, isLang } from '@/lib/i18n';
import { provider } from '@/lib/i18n-ui';
import { SITE } from '@/lib/site-config';
import '../global.css';

export const metadata: Metadata = {
  metadataBase: new URL(SITE.url),
  title: {
    default: 'Vortex — a data-oriented C++ game engine',
    template: '%s · Vortex',
  },
  description:
    'Code-first C++20 game engine. Data-oriented ECS, backend-neutral renderer (Vulkan + WebGPU), 2D today and 3D next. Ship a game in a few dozen lines.',
  openGraph: {
    type: 'website',
    siteName: 'Vortex',
  },
};

/** Both locales are prerendered; there is no dynamic locale. */
export function generateStaticParams() {
  return LANGUAGES.map((lang) => ({ lang }));
}

export default async function RootLayout({
  children,
  params,
}: {
  children: ReactNode;
  params: Promise<{ lang: string }>;
}) {
  const { lang } = await params;
  if (!isLang(lang)) notFound();

  return (
    <html lang={lang} suppressHydrationWarning>
      <body className="flex min-h-screen flex-col">
        <RootProvider i18n={provider(lang)}>{children}</RootProvider>
      </body>
    </html>
  );
}
