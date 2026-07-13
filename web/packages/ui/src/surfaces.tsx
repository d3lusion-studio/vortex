import type { HTMLAttributes, ReactNode } from 'react';
import { cn } from './cn';

/** A hairline-bordered panel. No shadow: depth is drawn with a line, not a blur. */
export function Card({ className, ...props }: HTMLAttributes<HTMLDivElement>) {
  return (
    <div
      className={cn(
        'rounded-none border border-[var(--border-subtle)] bg-[var(--surface-raised)] p-6',
        'transition-colors hover:border-[var(--border-strong)]',
        className,
      )}
      {...props}
    />
  );
}

/** A small square tag. The pixel face earns its keep here — it is three words long. */
export function Badge({ className, ...props }: HTMLAttributes<HTMLSpanElement>) {
  return (
    <span
      className={cn(
        'inline-flex items-center gap-1.5 rounded-none border border-[var(--border-strong)]',
        'bg-[var(--surface-raised)] px-2.5 py-1 font-mono text-[11px] tracking-wide',
        'text-[var(--text-muted)] uppercase',
        className,
      )}
      {...props}
    />
  );
}

interface SectionProps {
  eyebrow?: string;
  title: string;
  description?: string;
  children: ReactNode;
  className?: string;
}

/**
 * A titled section. Left-aligned rather than centred: centred headings look like a brochure,
 * left-aligned ones look like documentation, and this is documentation with a front door.
 */
export function Section({ eyebrow, title, description, children, className }: SectionProps) {
  return (
    <section
      className={cn(
        'mx-auto w-full max-w-5xl border-t border-[var(--border-subtle)] px-6 py-20',
        className,
      )}
    >
      <div className="max-w-2xl">
        {eyebrow && (
          <p className="mb-3 font-pixel text-[11px] tracking-[0.2em] text-[var(--accent)] uppercase">
            {eyebrow}
          </p>
        )}
        <h2 className="text-2xl font-semibold tracking-tight text-balance sm:text-3xl">{title}</h2>
        {description && (
          <p className="mt-3 text-pretty text-[var(--text-muted)]">{description}</p>
        )}
      </div>
      <div className="mt-12">{children}</div>
    </section>
  );
}
