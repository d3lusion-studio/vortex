import Link from 'next/link';
import { ButtonLink } from '@vortex/ui';
import type { Dictionary } from '@/content/dictionary';
import type { Lang } from '@/lib/i18n';
import { SITE } from '@/lib/site-config';
import { LanguageSwitcher } from './language-switcher';

/** Prefixes a path with the locale. English is prefix-free (it is the default locale). */
export function localePath(lang: Lang, path: string): string {
  return lang === 'en' ? path : `/${lang}${path === '/' ? '' : path}`;
}

export function SiteHeader({ lang, dict }: { lang: Lang; dict: Dictionary }) {
  const nav = [
    { href: '/docs', label: dict.nav.docs },
    { href: '/blog', label: dict.nav.blog },
    { href: '/assets', label: dict.nav.assets },
  ];

  return (
    <header className="sticky top-0 z-50 border-b border-[var(--border-subtle)] bg-[var(--surface)]/80 backdrop-blur-md">
      <div className="mx-auto flex h-14 w-full max-w-6xl items-center gap-8 px-6">
        <Link href={localePath(lang, '/')} className="font-mono text-sm font-semibold tracking-tight">
          <span className="text-accent-400">▲</span> Vortex
        </Link>
        <nav className="hidden gap-6 text-sm text-[var(--text-muted)] md:flex">
          {nav.map((item) => (
            <Link
              key={item.href}
              href={localePath(lang, item.href)}
              className="hover:text-[var(--text-primary)]"
            >
              {item.label}
            </Link>
          ))}
        </nav>
        <div className="ml-auto flex items-center gap-2">
          <LanguageSwitcher current={lang} label={dict.nav.language} />
          <ButtonLink
            href={SITE.repoUrl}
            variant="ghost"
            size="sm"
            target="_blank"
            rel="noreferrer"
          >
            {dict.nav.github}
          </ButtonLink>
          <ButtonLink href={localePath(lang, '/docs/getting-started')} size="sm">
            {dict.nav.getStarted}
          </ButtonLink>
        </div>
      </div>
    </header>
  );
}
