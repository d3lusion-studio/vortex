import type { AnchorHTMLAttributes, ButtonHTMLAttributes, ReactNode } from 'react';
import { cn } from './cn';

type Variant = 'primary' | 'secondary' | 'ghost';
type Size = 'sm' | 'md' | 'lg';

/**
 * Square, hard-edged, no shadow. The "press" is a 1px downward nudge — the cheapest possible
 * affordance, and the only one that fits a pixel grid.
 */
const VARIANTS: Record<Variant, string> = {
  primary:
    'bg-[var(--accent)] text-[var(--accent-contrast)] hover:brightness-110 active:translate-y-px',
  secondary:
    'bg-[var(--surface-raised)] text-[var(--text-primary)] border border-[var(--border-strong)] ' +
    'hover:border-[var(--accent)] hover:text-[var(--accent)] active:translate-y-px',
  ghost: 'text-[var(--text-muted)] hover:text-[var(--text-primary)]',
};

const SIZES: Record<Size, string> = {
  sm: 'h-8 px-3 text-[13px]',
  md: 'h-10 px-4 text-sm',
  lg: 'h-11 px-6 text-sm',
};

const BASE =
  'inline-flex items-center justify-center gap-2 rounded-none font-medium tracking-tight ' +
  'transition-[filter,color,border-color,transform] duration-75 ' +
  'focus-visible:outline focus-visible:outline-2 focus-visible:outline-offset-2 ' +
  'focus-visible:outline-[var(--accent)] disabled:pointer-events-none disabled:opacity-50';

interface Common {
  variant?: Variant;
  size?: Size;
  children: ReactNode;
  className?: string;
}

export function Button({
  variant = 'primary',
  size = 'md',
  className,
  ...props
}: Common & ButtonHTMLAttributes<HTMLButtonElement>) {
  return <button className={cn(BASE, VARIANTS[variant], SIZES[size], className)} {...props} />;
}

export function ButtonLink({
  variant = 'primary',
  size = 'md',
  className,
  ...props
}: Common & AnchorHTMLAttributes<HTMLAnchorElement>) {
  return <a className={cn(BASE, VARIANTS[variant], SIZES[size], className)} {...props} />;
}
