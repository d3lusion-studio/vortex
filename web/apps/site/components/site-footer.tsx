import Link from 'next/link';
import type { Dictionary } from '@/content/dictionary';
import type { Lang } from '@/lib/i18n';
import { SITE, discussionsUrl, roadmapUrl } from '@/lib/site-config';
import { localePath } from './site-header';

export function SiteFooter({ lang, dict }: { lang: Lang; dict: Dictionary }) {
  // Every link here resolves today. Terms/Privacy/Discord are deliberately absent until they
  // exist — a footer full of 404s is worse than a short footer.
  const columns = [
    {
      title: dict.footer.product,
      links: [
        { href: localePath(lang, '/docs'), label: dict.footer.documentation },
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
      <div className="mx-auto grid w-full max-w-6xl gap-10 px-6 py-14 sm:grid-cols-2 md:grid-cols-4">
        <div className="md:col-span-2">
          <p className="font-mono text-sm font-semibold">
            <span className="text-accent-400">▲</span> Vortex
          </p>
          <p className="mt-3 max-w-xs text-sm text-[var(--text-muted)]">{dict.footer.tagline}</p>
        </div>
        {columns.map((col) => (
          <div key={col.title}>
            <p className="text-sm font-semibold">{col.title}</p>
            <ul className="mt-3 space-y-2 text-sm text-[var(--text-muted)]">
              {col.links.map((link) => (
                <li key={link.href}>
                  <Link href={link.href} className="hover:text-[var(--text-primary)]">
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
