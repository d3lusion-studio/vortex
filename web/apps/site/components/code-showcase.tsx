'use client';

import { useState } from 'react';
import { cn } from '@vortex/ui';

export interface Snippet {
  id: string;
  label: string;
  /** Pre-highlighted HTML from `lib/highlight.ts`; highlighting never ships to the client. */
  html: string;
  caption: string;
}

export function CodeShowcase({ snippets }: { snippets: Snippet[] }) {
  const [active, setActive] = useState(snippets[0]?.id ?? '');
  const current = snippets.find((s) => s.id === active) ?? snippets[0];
  if (!current) return null;

  return (
    <div className="border border-[var(--border-subtle)] bg-[var(--surface-raised)]">
      <div
        role="tablist"
        aria-label="Code examples"
        className="flex overflow-x-auto border-b border-[var(--border-subtle)]"
      >
        {snippets.map((s) => (
          <button
            key={s.id}
            role="tab"
            aria-selected={s.id === active}
            onClick={() => setActive(s.id)}
            className={cn(
              // The active tab is marked by a 2px rule, not a filled pill: it keeps the header
              // flat and puts the emphasis on the code below rather than the chrome above.
              'border-b-2 px-4 py-2.5 text-[13px] whitespace-nowrap transition-colors',
              s.id === active
                ? 'border-[var(--accent)] text-[var(--text-primary)]'
                : 'border-transparent text-[var(--text-muted)] hover:text-[var(--text-primary)]',
            )}
          >
            {s.label}
          </button>
        ))}
      </div>

      <div
        role="tabpanel"
        className="overflow-x-auto p-5 text-[13px] leading-relaxed [&_pre]:bg-transparent! [&_pre]:font-mono"
        dangerouslySetInnerHTML={{ __html: current.html }}
      />

      <p className="border-t border-[var(--border-subtle)] bg-[var(--surface-sunken)] px-5 py-2.5 font-mono text-[11px] text-[var(--text-muted)]">
        {current.caption}
      </p>
    </div>
  );
}
