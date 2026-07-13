'use client';

import { usePathname } from 'next/navigation';
import { useRouter } from 'next/navigation';
import { cn } from '@vortex/ui';
import { LANGUAGES, type Lang } from '@/lib/i18n';

const LABELS: Record<Lang, string> = { en: 'EN', vi: 'VI' };

/**
 * Swaps the locale segment of the current path, so the reader stays on the page they were on.
 *
 * English is the default locale and is served without a prefix (`/docs`), Vietnamese carries one
 * (`/vi/docs`) — the middleware rewrites, so both are real URLs and this is pure string work.
 */
export function LanguageSwitcher({ current, label }: { current: Lang; label: string }) {
  const pathname = usePathname();
  const router = useRouter();

  function pathFor(target: Lang): string {
    // Strip the current prefix down to a locale-free path, then re-prefix.
    const bare = pathname.replace(/^\/(vi|en)(?=\/|$)/, '') || '/';
    if (target === 'en') return bare;
    return bare === '/' ? '/vi' : `/vi${bare}`;
  }

  return (
    <div
      className="flex items-center border border-[var(--border-subtle)]"
      role="group"
      aria-label={label}
    >
      {LANGUAGES.map((lang) => (
        <button
          key={lang}
          type="button"
          onClick={() => router.push(pathFor(lang))}
          aria-current={lang === current}
          className={cn(
            'px-2 py-1 font-mono text-[11px] transition-colors',
            lang === current
              ? 'bg-[var(--accent)] text-[var(--accent-contrast)]'
              : 'text-[var(--text-muted)] hover:text-[var(--text-primary)]',
          )}
        >
          {LABELS[lang]}
        </button>
      ))}
    </div>
  );
}
