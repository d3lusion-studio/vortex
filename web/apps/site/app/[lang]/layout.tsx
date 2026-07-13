import type { Metadata } from 'next';
import { notFound } from 'next/navigation';
import { Inter, JetBrains_Mono, Silkscreen } from 'next/font/google';
import { RootProvider } from 'fumadocs-ui/provider/next';
import type { ReactNode } from 'react';
import { LANGUAGES, isLang } from '@/lib/i18n';
import { provider } from '@/lib/i18n-ui';
import { SITE } from '@/lib/site-config';
import '../global.css';

// Self-hosted by next/font — no request ever leaves for a font CDN, and no layout shift.
const inter = Inter({
  subsets: ['latin', 'vietnamese'],
  variable: '--font-inter',
  display: 'swap',
});

const jetbrains = JetBrains_Mono({
  subsets: ['latin'],
  variable: '--font-mono-code',
  display: 'swap',
});

// The pixel face. Used for the wordmark and section labels only: it has no Vietnamese diacritics
// and no lowercase rhythm worth reading at paragraph length.
const silkscreen = Silkscreen({
  subsets: ['latin'],
  weight: '400',
  variable: '--font-pixel-display',
  display: 'swap',
});

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
    <html
      lang={lang}
      className={`${inter.variable} ${jetbrains.variable} ${silkscreen.variable}`}
      suppressHydrationWarning
    >
      <body className="flex min-h-screen flex-col">
        <RootProvider i18n={provider(lang)}>{children}</RootProvider>
      </body>
    </html>
  );
}
