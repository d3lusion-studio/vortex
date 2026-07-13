import Link from 'next/link';
import { ButtonLink } from '@vortex/ui';
import type { Dictionary } from '@/content/dictionary';
import type { Lang } from '@/lib/i18n';
import { SITE } from '@/lib/site-config';
import { LanguageSwitcher } from './language-switcher';
import { Wordmark } from './wordmark';

/** Prefixes a path with the locale. English is prefix-free (it is the default locale). */
export function localePath(lang: Lang, path: string): string {
  return lang === 'en' ? path : `/${lang}${path === '/' ? '' : path}`;
}

export function SiteHeader({ lang, dict }: { lang: Lang; dict: Dictionary }) {
  const nav = [
    { href: '/learn', label: dict.nav.learn },
    { href: '/blog', label: dict.nav.blog },
    { href: '/assets', label: dict.nav.assets },
  ];

  return (
    <header className="sticky top-0 z-50 border-b border-[var(--border-subtle)] bg-[var(--surface)]/85 backdrop-blur">
      <div className="mx-auto flex h-14 w-full max-w-5xl items-center gap-8 px-6">
        <Link href={localePath(lang, '/')} aria-label="Vortex">
          <Wordmark />
        </Link>

        <nav className="hidden gap-6 text-sm text-[var(--text-muted)] md:flex">
          {nav.map((item) => (
            <Link
              key={item.href}
              href={localePath(lang, item.href)}
              className="transition-colors hover:text-[var(--text-primary)]"
            >
              {item.label}
            </Link>
          ))}
        </nav>

        <div className="ml-auto flex items-center gap-3">
          <LanguageSwitcher current={lang} label={dict.nav.language} />
          <span className="hidden h-4 w-px bg-[var(--border-subtle)] sm:block" />
          <Link
            href={SITE.repoUrl}
            target="_blank"
            rel="noreferrer"
            className="hidden text-sm text-[var(--text-muted)] transition-colors hover:text-[var(--text-primary)] sm:block"
          >
            {dict.nav.github}
          </Link>
          <ButtonLink href={localePath(lang, '/learn/getting-started')} size="sm">
            {dict.nav.getStarted}
          </ButtonLink>
        </div>
      </div>
    </header>
  );
}
