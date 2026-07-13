import type { Metadata } from 'next';
import Link from 'next/link';
import { Section } from '@vortex/ui';
import { getDictionary } from '@/content/dictionary';
import { allPosts } from '@/lib/blog';
import { toLang } from '@/lib/i18n';

interface Props {
  params: Promise<{ lang: string }>;
}

export async function generateMetadata({ params }: Props): Promise<Metadata> {
  const { lang: rawLang } = await params;
  const lang = toLang(rawLang);
  const dict = getDictionary(lang);
  return { title: dict.blog.title, description: dict.blog.description };
}

export default async function BlogIndex({ params }: Props) {
  const { lang: rawLang } = await params;
  const lang = toLang(rawLang);
  const dict = getDictionary(lang);
  const posts = allPosts(lang);

  return (
    <Section
      eyebrow={dict.blog.eyebrow}
      title={dict.blog.title}
      description={dict.blog.description}
    >
      <div className="divide-y divide-[var(--border-subtle)] border-y border-[var(--border-subtle)]">
        {posts.map((post) => (
          <Link
            key={post.url}
            href={post.url}
            className="group block py-6 transition-colors hover:bg-[var(--surface-sunken)]"
          >
            <time className="font-mono text-[11px] text-[var(--text-muted)]">{post.data.date}</time>
            <h3 className="mt-1.5 text-lg font-semibold tracking-tight group-hover:text-[var(--accent)]">
              {post.data.title}
            </h3>
            <p className="mt-1.5 max-w-2xl text-sm leading-relaxed text-[var(--text-muted)]">
              {post.data.description}
            </p>
          </Link>
        ))}
      </div>
    </Section>
  );
}
