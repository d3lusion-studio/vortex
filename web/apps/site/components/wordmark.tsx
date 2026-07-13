import { cn } from '@vortex/ui';

/**
 * The logo, drawn as pixels rather than shipped as an SVG.
 *
 * Nine squares on a 3×3 lattice with the centre knocked out — a vortex read literally: matter
 * circling a hole. It is CSS, so it inherits the accent colour and stays crisp at any zoom, and it
 * costs no request.
 */
export function Wordmark({ className }: { className?: string }) {
  return (
    <span className={cn('flex items-center gap-2', className)}>
      <span
        aria-hidden
        className="grid size-[14px] grid-cols-3 grid-rows-3 gap-px"
        style={{ ['--dot' as string]: 'var(--accent)' }}
      >
        {Array.from({ length: 9 }, (_, i) => (
          <span
            key={i}
            className={cn(
              'block',
              // The hole. Everything else is matter.
              i === 4 ? 'bg-transparent' : 'bg-[var(--accent)]',
              // Two corners dimmed, so the ring reads as rotating rather than sitting still.
              (i === 0 || i === 8) && 'opacity-40',
            )}
          />
        ))}
      </span>
      <span className="font-pixel text-[13px] tracking-tight">VORTEX</span>
    </span>
  );
}
