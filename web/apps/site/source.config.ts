import { defineCollections, defineConfig, defineDocs, frontmatterSchema } from 'fumadocs-mdx/config';
import { z } from 'zod';

/**
 * Docs live in `content/docs` as MDX. The engine's hand-written guides in the repo's top-level
 * `docs/` folder are the upstream source; they are ported here once and then maintained here,
 * because the site needs frontmatter, versioning and MDX components that plain Markdown can't carry.
 */
export const docs = defineDocs({
  dir: 'content/docs',
});

/** Release notes and engineering posts. Every engine release gets one — this is how an
 * open-source engine grows an audience. */
export const blog = defineCollections({
  type: 'doc',
  dir: 'content/blog',
  schema: frontmatterSchema.extend({
    date: z.string(),
    author: z.string(),
  }),
});

export default defineConfig({
  mdxOptions: {
    // Syntax highlighting themes for the C++ snippets that dominate these docs.
    rehypeCodeOptions: {
      themes: {
        light: 'github-light',
        dark: 'github-dark',
      },
    },
  },
});
