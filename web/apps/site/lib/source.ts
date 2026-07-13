import { docs } from '@/.source/server';
import { loader } from 'fumadocs-core/source';
import { i18n } from './i18n';

/**
 * The docs content tree, one tree per language.
 *
 * With `parser: 'dot'`, `architecture.mdx` is English and `architecture.vi.mdx` is its
 * translation. A page with no `.vi.mdx` falls back to English rather than vanishing.
 */
export const source = loader({
  baseUrl: '/docs',
  i18n,
  source: docs.toFumadocsSource(),
});
