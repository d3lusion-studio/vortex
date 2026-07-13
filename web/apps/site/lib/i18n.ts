import { defineI18n } from 'fumadocs-core/i18n';

export const LANGUAGES = ['en', 'vi'] as const;
export type Lang = (typeof LANGUAGES)[number];

/**
 * English lives at `/`, Vietnamese at `/vi`.
 *
 * `hideLocale: 'default-locale'` keeps the English URLs prefix-free — they are the canonical
 * ones for search engines, and the site had them before i18n existed, so nothing 404s.
 *
 * `parser: 'dot'` means a translation is a sibling file: `index.mdx` -> `index.vi.mdx`.
 * `fallbackLanguage: 'en'` means an untranslated page shows the English text instead of
 * disappearing from the Vietnamese sidebar.
 */
export const i18n = defineI18n({
  languages: [...LANGUAGES],
  defaultLanguage: 'en',
  hideLocale: 'default-locale',
  parser: 'dot',
  fallbackLanguage: 'en',
});

export function isLang(value: string): value is Lang {
  return (LANGUAGES as readonly string[]).includes(value);
}

/**
 * Next's generated route types hand `params.lang` over as a plain `string`, so every page narrows
 * it here. An unknown locale never reaches a page in practice — the proxy only rewrites known
 * ones — so falling back to English is a defensive default, not a code path users hit.
 */
export function toLang(value: string): Lang {
  return isLang(value) ? value : 'en';
}
