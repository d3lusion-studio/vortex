import type { HTMLAttributes, ReactNode } from 'react';
import { cn } from './cn';

export function Card({ className, ...props }: HTMLAttributes<HTMLDivElement>) {
  return (
    <div
      className={cn(
        'rounded-[var(--radius-card)] border border-[var(--border-subtle)]',
        'bg-[var(--surface-raised)] p-6',
        className,
      )}
      {...props}
    />
  );
}

export function Badge({ className, ...props }: HTMLAttributes<HTMLSpanElement>) {
  return (
    <span
      className={cn(
        'inline-flex items-center gap-1.5 rounded-full border border-accent-500/30',
        'bg-accent-500/10 px-3 py-1 text-xs font-medium text-accent-300',
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

/** A titled page section. Keeps vertical rhythm identical across the whole site. */
export function Section({ eyebrow, title, description, children, className }: SectionProps) {
  return (
    <section className={cn('mx-auto w-full max-w-6xl px-6 py-20 sm:py-28', className)}>
      <div className="mx-auto max-w-2xl text-center">
        {eyebrow && (
          <p className="mb-3 text-sm font-semibold tracking-widest text-accent-400 uppercase">
            {eyebrow}
          </p>
        )}
        <h2 className="text-3xl font-semibold tracking-tight text-balance sm:text-4xl">{title}</h2>
        {description && (
          <p className="mt-4 text-lg text-pretty text-[var(--text-muted)]">{description}</p>
        )}
      </div>
      <div className="mt-14">{children}</div>
    </section>
  );
}
