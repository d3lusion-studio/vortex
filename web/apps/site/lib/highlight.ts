import { codeToHtml } from 'shiki';

/**
 * Server-side syntax highlighting for marketing snippets.
 *
 * Docs code blocks are highlighted by fumadocs at MDX compile time; this is only for the
 * hand-placed snippets on landing/showcase pages, which are plain strings.
 */
export function highlight(code: string, lang = 'cpp'): Promise<string> {
  return codeToHtml(code, {
    lang,
    themes: { light: 'github-light', dark: 'github-dark' },
    defaultColor: false,
  });
}
