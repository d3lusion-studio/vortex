import { clsx, type ClassValue } from 'clsx';
import { twMerge } from 'tailwind-merge';

/** Merge Tailwind classes so a caller's class always wins over a component default. */
export function cn(...inputs: ClassValue[]): string {
  return twMerge(clsx(inputs));
}
