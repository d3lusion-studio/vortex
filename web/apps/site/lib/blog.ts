import { blog } from '@/.source/server';
import { loader } from 'fumadocs-core/source';
import { toFumadocsSource } from 'fumadocs-mdx/runtime/server';
import { i18n, type Lang } from './i18n';

/**
 * `blog` is a plain doc collection (no meta.json tree), so it is turned into a source with the
 * standalone helper rather than the `.toFumadocsSource()` a docs collection carries.
 */
export const blogSource = loader({
  baseUrl: '/blog',
  i18n,
  source: toFumadocsSource(blog, []),
});

export type Post = ReturnType<typeof blogSource.getPages>[number];

/** Posts for one language, newest first. Untranslated posts fall back to English. */
export function allPosts(lang: Lang): Post[] {
  return [...blogSource.getPages(lang)].sort(
    (a, b) => new Date(b.data.date).getTime() - new Date(a.data.date).getTime(),
  );
}
