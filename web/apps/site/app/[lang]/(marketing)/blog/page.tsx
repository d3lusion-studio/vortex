import type { Metadata } from 'next';
import Link from 'next/link';
import { Card, Section } from '@vortex/ui';
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
      <div className="mx-auto grid max-w-3xl gap-5">
        {posts.map((post) => (
          <Link key={post.url} href={post.url}>
            <Card className="transition-colors hover:border-accent-500/40">
              <time className="font-mono text-xs text-accent-400">{post.data.date}</time>
              <h3 className="mt-2 text-xl font-semibold">{post.data.title}</h3>
              <p className="mt-2 text-sm text-[var(--text-muted)]">{post.data.description}</p>
            </Card>
          </Link>
        ))}
      </div>
    </Section>
  );
}
