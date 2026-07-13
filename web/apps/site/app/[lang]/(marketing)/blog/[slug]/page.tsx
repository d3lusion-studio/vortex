import { notFound } from 'next/navigation';
import type { Metadata } from 'next';
import defaultMdxComponents from 'fumadocs-ui/mdx';
import { getDictionary } from '@/content/dictionary';
import { blogSource } from '@/lib/blog';
import { LANGUAGES, toLang } from '@/lib/i18n';

interface Props {
  params: Promise<{ lang: string; slug: string }>;
}

export default async function BlogPost({ params }: Props) {
  const { lang: rawLang, slug } = await params;
  const lang = toLang(rawLang);
  const page = blogSource.getPage([slug], lang);
  if (!page) notFound();

  const dict = getDictionary(lang);
  const post = page.data;
  const MDX = post.body;

  return (
    <article className="mx-auto w-full max-w-3xl px-6 py-20">
      <time className="font-mono text-xs text-accent-400">{post.date}</time>
      <h1 className="mt-3 text-4xl font-semibold tracking-tight text-balance">{post.title}</h1>
      <p className="mt-3 text-sm text-[var(--text-muted)]">
        {dict.blog.by} {post.author}
      </p>
      <div className="prose prose-invert mt-10 max-w-none">
        <MDX components={defaultMdxComponents} />
      </div>
    </article>
  );
}

export function generateStaticParams() {
  return LANGUAGES.flatMap((lang) =>
    blogSource.getPages(lang).map((post) => ({ lang, slug: post.slugs[0]! })),
  );
}

export async function generateMetadata({ params }: Props): Promise<Metadata> {
  const { lang: rawLang, slug } = await params;
  const lang = toLang(rawLang);
  const page = blogSource.getPage([slug], lang);
  if (!page) notFound();

  return { title: page.data.title, description: page.data.description };
}
