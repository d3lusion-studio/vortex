import { createMDX } from 'fumadocs-mdx/next';

const withMDX = createMDX();

/** @type {import('next').NextConfig} */
const config = {
  reactStrictMode: true,
  // The shared UI package ships TypeScript source, not a build output, so Next compiles it.
  transpilePackages: ['@vortex/ui'],
};

export default withMDX(config);
