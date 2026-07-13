import { defineCollections, defineConfig, defineDocs, frontmatterSchema } from 'fumadocs-mdx/config';
import { z } from 'zod';

/**
 * The Learn section: guides, not an API dump. Lives in `content/learn` as MDX, translated as
 * sibling `*.vi.mdx` files.
 */
export const docs = defineDocs({
  dir: 'content/learn',
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
