import { createI18nMiddleware } from 'fumadocs-core/i18n/middleware';
import { i18n } from '@/lib/i18n';

export default createI18nMiddleware(i18n);

export const config = {
  // Never rewrite: API routes, Next internals, the compiled WASM demos, or any file with an
  // extension. Rewriting /wasm/* would break the Emscripten loader's relative fetches.
  matcher: ['/((?!api|_next|wasm|.*\\..*).*)'],
};
