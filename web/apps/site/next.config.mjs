import { createMDX } from 'fumadocs-mdx/next';

const withMDX = createMDX();

/** @type {import('next').NextConfig} */
const config = {
  reactStrictMode: true,
  // The shared UI package ships TypeScript source, not a build output, so Next compiles it.
  transpilePackages: ['@vortex/ui'],
  async redirects() {
    // The section used to live at /docs. Permanent, so anything that linked to it keeps working
    // and search engines move their index across rather than accumulating 404s.
    return [
      { source: '/docs', destination: '/learn', permanent: true },
      { source: '/docs/:path*', destination: '/learn/:path*', permanent: true },
      { source: '/vi/docs', destination: '/vi/learn', permanent: true },
      { source: '/vi/docs/:path*', destination: '/vi/learn/:path*', permanent: true },
    ];
  },
};

export default withMDX(config);
