import { createFromSource } from 'fumadocs-core/search/server';
import { source } from '@/lib/source';

/**
 * One static search index per language — the source carries the i18n config, so `createFromSource`
 * builds a tree per locale and the client passes its own.
 *
 * Orama has no Vietnamese stemmer, and left to itself it hands the locale code `vi` to Orama as a
 * language name, which it does not recognise — the Vietnamese index then comes back empty for every
 * query. Mapping it to the English tokenizer keeps the index working: Vietnamese words are
 * space-separated, so whitespace tokenization is correct here; only stemming is lost, which
 * Vietnamese does not need (it is not an inflected language).
 */
export const { GET } = createFromSource(source, {
  localeMap: {
    en: 'english',
    vi: { language: 'english' },
  },
});
