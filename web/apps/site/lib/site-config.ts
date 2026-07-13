/**
 * Facts about the project that appear in more than one place. Anything hardcoded in two
 * components is a fact waiting to diverge — the repository URL already did.
 *
 * Nothing here may be aspirational: every URL must resolve today. A dead link in the footer of a
 * project's own homepage is the cheapest possible way to look unfinished.
 */
export const SITE = {
  repoUrl: 'https://github.com/d3lusion-studio/vortex',

  /**
   * Absolute URL of the deployed site, used for canonical links and OG tags. There is no domain
   * yet, so it defaults to localhost and is overridden at build time on the host
   * (Vercel sets VERCEL_PROJECT_PRODUCTION_URL; anything else can set NEXT_PUBLIC_SITE_URL).
   */
  url:
    process.env.NEXT_PUBLIC_SITE_URL ??
    (process.env.VERCEL_PROJECT_PRODUCTION_URL
      ? `https://${process.env.VERCEL_PROJECT_PRODUCTION_URL}`
      : 'http://localhost:3000'),
} as const;

/** Link to a file in the repository at the default branch. */
export function repoFile(path: string): string {
  return `${SITE.repoUrl}/blob/main/${path}`;
}

export const discussionsUrl = `${SITE.repoUrl}/discussions`;
export const roadmapUrl = repoFile('engine_roadmap.md');
