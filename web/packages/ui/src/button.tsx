import type { AnchorHTMLAttributes, ButtonHTMLAttributes, ReactNode } from 'react';
import { cn } from './cn';

type Variant = 'primary' | 'secondary' | 'ghost';
type Size = 'sm' | 'md' | 'lg';

const VARIANTS: Record<Variant, string> = {
  primary:
    'bg-accent-500 text-ink-950 hover:bg-accent-400 shadow-[0_8px_30px_-8px_rgb(6_182_212/0.6)]',
  secondary:
    'bg-white/5 text-[var(--text-primary)] ring-1 ring-inset ring-white/12 hover:bg-white/10',
  ghost: 'text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-white/5',
};

const SIZES: Record<Size, string> = {
  sm: 'h-8 px-3 text-sm',
  md: 'h-10 px-4 text-sm',
  lg: 'h-12 px-6 text-base',
};

const BASE =
  'inline-flex items-center justify-center gap-2 rounded-lg font-medium transition-colors ' +
  'focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-accent-400 ' +
  'disabled:pointer-events-none disabled:opacity-50';

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
