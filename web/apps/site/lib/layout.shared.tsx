import type { BaseLayoutProps } from 'fumadocs-ui/layouts/shared';
import { getDictionary } from '@/content/dictionary';
import { localePath } from '@/components/site-header';
import { Wordmark } from '@/components/wordmark';
import type { Lang } from './i18n';
import { SITE } from './site-config';

/** Nav shared by the docs layout and any other fumadocs-driven layout. */
export function baseOptions(lang: Lang): BaseLayoutProps {
  const dict = getDictionary(lang);

  return {
    i18n: true,
    nav: {
      title: <Wordmark />,
      url: localePath(lang, '/'),
    },
    links: [
      { text: dict.nav.learn, url: localePath(lang, '/learn'), active: 'nested-url' },
      { text: dict.nav.blog, url: localePath(lang, '/blog') },
      { text: dict.nav.assets, url: localePath(lang, '/assets') },
    ],
    githubUrl: SITE.repoUrl,
  };
}
