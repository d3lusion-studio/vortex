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
    <div className="overflow-hidden rounded-[var(--radius-card)] border border-[var(--border-subtle)] bg-[var(--surface-raised)]">
      <div
        role="tablist"
        aria-label="Code examples"
        className="flex gap-1 overflow-x-auto border-b border-[var(--border-subtle)] p-2"
      >
        {snippets.map((s) => (
          <button
            key={s.id}
            role="tab"
            aria-selected={s.id === active}
            onClick={() => setActive(s.id)}
            className={cn(
              'rounded-md px-3 py-1.5 text-sm whitespace-nowrap transition-colors',
              s.id === active
                ? 'bg-accent-500/15 text-accent-300'
                : 'text-[var(--text-muted)] hover:text-[var(--text-primary)]',
            )}
          >
            {s.label}
          </button>
        ))}
      </div>

      <div
        role="tabpanel"
        className="overflow-x-auto p-5 text-sm [&_pre]:bg-transparent! [&_pre]:font-mono"
        dangerouslySetInnerHTML={{ __html: current.html }}
      />

      <p className="border-t border-[var(--border-subtle)] px-5 py-3 text-xs text-[var(--text-muted)]">
        {current.caption}
      </p>
    </div>
  );
}
