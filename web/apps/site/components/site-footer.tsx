import Link from 'next/link';
import type { Dictionary } from '@/content/dictionary';
import type { Lang } from '@/lib/i18n';
import { SITE, discussionsUrl, roadmapUrl } from '@/lib/site-config';
import { localePath } from './site-header';
import { Wordmark } from './wordmark';

export function SiteFooter({ lang, dict }: { lang: Lang; dict: Dictionary }) {
  // Every link here resolves today. Terms/Privacy/Discord are deliberately absent until they
  // exist — a footer full of 404s is worse than a short footer.
  const columns = [
    {
      title: dict.footer.product,
      links: [
        { href: localePath(lang, '/learn'), label: dict.footer.learn },
        { href: localePath(lang, '/assets'), label: dict.footer.registry },
        { href: roadmapUrl, label: dict.footer.roadmap },
      ],
    },
    {
      title: dict.footer.community,
      links: [
        { href: SITE.repoUrl, label: dict.nav.github },
        { href: discussionsUrl, label: dict.footer.discussions },
        { href: localePath(lang, '/blog'), label: dict.nav.blog },
      ],
    },
  ];

  return (
    <footer className="mt-auto border-t border-[var(--border-subtle)]">
      <div className="mx-auto grid w-full max-w-5xl gap-10 px-6 py-14 sm:grid-cols-2 md:grid-cols-4">
        <div className="md:col-span-2">
          <Wordmark />
          <p className="mt-4 max-w-xs text-sm leading-relaxed text-[var(--text-muted)]">
            {dict.footer.tagline}
          </p>
        </div>
        {columns.map((col) => (
          <div key={col.title}>
            <p className="font-pixel text-[10px] tracking-[0.2em] text-[var(--text-primary)] uppercase">
              {col.title}
            </p>
            <ul className="mt-3 space-y-2 text-sm text-[var(--text-muted)]">
              {col.links.map((link) => (
                <li key={link.href}>
                  <Link href={link.href} className="transition-colors hover:text-[var(--text-primary)]">
                    {link.label}
                  </Link>
                </li>
              ))}
            </ul>
          </div>
        ))}
      </div>
    </footer>
  );
}
