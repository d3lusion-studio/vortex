import type { Lang } from '@/lib/i18n';

/**
 * All marketing copy, in one place.
 *
 * Docs and blog are translated as MDX files; everything else on the site — nav, hero, feature
 * cards, the engine viewport's loading and error states — lives here. Components take strings
 * as props rather than importing this, so nothing renders a hardcoded language.
 *
 * `Dictionary` is derived from the English entry, so adding a key without translating it is a
 * type error rather than a silently English string on the Vietnamese site.
 */
const en = {
  nav: {
    docs: 'Docs',
    blog: 'Blog',
    assets: 'Assets',
    github: 'GitHub',
    getStarted: 'Get started',
    language: 'Language',
  },
  hero: {
    badge: 'C++20 · Vulkan and WebGPU',
    titleLead: 'A game engine you can',
    titleAccent: 'read all the way down',
    subtitle:
      'Vortex is a data-oriented C++20 engine with a backend-neutral renderer. You own the stack from the GPU command buffer up to the gameplay API — and it ships to the web.',
    ctaPrimary: 'Get started',
    ctaSecondary: 'View the source',
  },
  api: {
    eyebrow: 'The API',
    title: 'A game in a few dozen lines',
    description:
      'Every snippet below is lifted from the examples in the repository, not written for the brochure.',
  },
  features: {
    eyebrow: 'Architecture',
    title: 'Built to survive its own roadmap',
    description:
      'Every big decision had to pass one test: if we add mesh rendering, PBR and shadows tomorrow, do we rewrite this layer? We did not.',
    items: [
      {
        title: 'Data-oriented ECS',
        body: 'Sparse-set registry, packed component arrays, systems as plain functions. Extraction and culling run over flat arrays and parallelise across the job system.',
      },
      {
        title: 'Backend-neutral renderer',
        body: 'An RHI seam splits gameplay from the GPU. Vulkan and WebGPU are two implementations behind it, driven by the same renderer code. DX12 and Metal slot in without touching a line above the seam.',
      },
      {
        title: '2D that ships, 3D that scales',
        body: 'Sprite batching, tilemaps with Tiled import, 2D physics via Box2D, text and UI. The 3D path is live too: GGX PBR, PCF shadows, HDR bloom with ACES tonemapping.',
      },
      {
        title: 'Code-first, no editor tax',
        body: 'Spawn an entity, attach components, run. No scene tree to fight, no proprietary project format. Your game is a C++ program that links a library.',
      },
      {
        title: 'Modern C++20',
        body: 'Concepts, std::span, designated initialisers. Strict warnings, no compiler extensions, CMake presets and compile_commands.json out of the box.',
      },
    ],
  },
  closing: {
    title: 'Start with the triangle. Finish with a game.',
    ctaDocs: 'Read the docs',
    ctaGithub: 'Star on GitHub',
    assetsPrompt: 'Looking for assets and plugins?',
    assetsLink: 'Browse the registry',
  },
  blog: {
    eyebrow: 'Blog',
    title: 'Release notes & engineering',
    description:
      'Every release gets a write-up. No changelog dumps — what changed, and why it changed.',
    by: 'by',
  },
  assets: {
    eyebrow: 'Registry',
    title: 'Assets and plugins, coming in stages',
    description:
      'The registry is being built in the open. Here is exactly where it stands, so nobody plans a business around a page that does not exist yet.',
    plan: [
      {
        phase: 'Phase 1 — free registry',
        body: 'Content-addressed .vpack packages, semver, engine-compat ranges, and a `vortex add <pkg>` CLI backed by a public API. Free and open, like crates.io.',
      },
      {
        phase: 'Phase 2 — first-party packs',
        body: 'Paid asset packs and project templates published by the Vortex team, sold through a merchant-of-record so tax and VAT are handled for buyers worldwide.',
      },
      {
        phase: 'Phase 3 — third-party sellers',
        body: 'Creator accounts, split payouts, reviews, and a seller dashboard. Gated behind a real payments entity — we would rather ship this late than ship it broken.',
      },
    ],
  },
  footer: {
    tagline: 'An open-source, data-oriented C++ game engine.',
    product: 'Product',
    community: 'Community',
    documentation: 'Documentation',
    roadmap: 'Roadmap',
    registry: 'Asset registry',
    discussions: 'Discussions',
  },
};

// No `as const`: the type must describe the *shape*, not the English literals, or the Vietnamese
// dictionary could never satisfy it.
export type Dictionary = typeof en;

const vi: Dictionary = {
  nav: {
    docs: 'Tài liệu',
    blog: 'Blog',
    assets: 'Tài nguyên',
    github: 'GitHub',
    getStarted: 'Bắt đầu',
    language: 'Ngôn ngữ',
  },
  hero: {
    badge: 'C++20 · Vulkan và WebGPU',
    titleLead: 'Một game engine bạn có thể',
    titleAccent: 'đọc hiểu tới tận đáy',
    subtitle:
      'Vortex là engine C++20 hướng dữ liệu với renderer độc lập backend. Bạn sở hữu toàn bộ stack, từ command buffer của GPU lên tới API gameplay — và nó chạy được trên web.',
    ctaPrimary: 'Bắt đầu',
    ctaSecondary: 'Xem mã nguồn',
  },
  api: {
    eyebrow: 'API',
    title: 'Một game gói gọn trong vài chục dòng',
    description:
      'Mọi đoạn code dưới đây được lấy từ example thật trong repo, không phải code viết cho quảng cáo.',
  },
  features: {
    eyebrow: 'Kiến trúc',
    title: 'Thiết kế để sống sót qua chính roadmap của nó',
    description:
      'Mọi quyết định lớn đều phải qua một bài test: nếu mai thêm mesh rendering, PBR và shadow, có phải viết lại tầng này không? Câu trả lời là không.',
    items: [
      {
        title: 'ECS hướng dữ liệu',
        body: 'Registry dạng sparse-set, mảng component đóng gói liên tục, system là hàm thuần. Extract và culling chạy trên mảng phẳng và song song hóa được qua job system.',
      },
      {
        title: 'Renderer độc lập backend',
        body: 'Lớp RHI tách gameplay khỏi GPU. Vulkan và WebGPU là hai hiện thực nằm sau nó, cùng một code renderer điều khiển cả hai. DX12 và Metal cắm vào được mà không đụng một dòng nào phía trên lớp này.',
      },
      {
        title: '2D ship được, 3D mở rộng được',
        body: 'Sprite batching, tilemap với import từ Tiled, physics 2D qua Box2D, text và UI. Nhánh 3D cũng đã chạy: PBR GGX, shadow PCF, HDR bloom với tonemap ACES.',
      },
      {
        title: 'Code-first, không tốn phí editor',
        body: 'Spawn entity, gắn component, chạy. Không phải vật lộn với scene tree, không có định dạng project độc quyền. Game của bạn là một chương trình C++ link tới một thư viện.',
      },
      {
        title: 'C++20 hiện đại',
        body: 'Concepts, std::span, designated initialiser. Cảnh báo nghiêm ngặt, không dùng extension của compiler, có sẵn CMake preset và compile_commands.json.',
      },
    ],
  },
  closing: {
    title: 'Bắt đầu từ tam giác. Kết thúc bằng một trò chơi.',
    ctaDocs: 'Đọc tài liệu',
    ctaGithub: 'Star trên GitHub',
    assetsPrompt: 'Đang tìm asset và plugin?',
    assetsLink: 'Xem registry',
  },
  blog: {
    eyebrow: 'Blog',
    title: 'Release notes & kỹ thuật',
    description:
      'Mỗi bản phát hành đều có một bài viết. Không dump changelog — thay đổi cái gì, và vì sao.',
    by: 'bởi',
  },
  assets: {
    eyebrow: 'Registry',
    title: 'Asset và plugin, ra mắt theo từng giai đoạn',
    description:
      'Registry đang được xây công khai. Đây là hiện trạng chính xác, để không ai lên kế hoạch kinh doanh dựa trên một trang chưa tồn tại.',
    plan: [
      {
        phase: 'Giai đoạn 1 — registry miễn phí',
        body: 'Gói .vpack định địa chỉ theo nội dung, semver, khoảng tương thích engine, và CLI `vortex add <pkg>` chạy trên API công khai. Miễn phí và mở, như crates.io.',
      },
      {
        phase: 'Giai đoạn 2 — gói do đội ngũ phát hành',
        body: 'Asset pack và project template trả phí do chính đội Vortex phát hành, bán qua merchant-of-record để thuế và VAT được xử lý sẵn cho người mua toàn cầu.',
      },
      {
        phase: 'Giai đoạn 3 — người bán thứ ba',
        body: 'Tài khoản creator, chia tiền tự động, đánh giá, và dashboard người bán. Chỉ mở khi đã có pháp nhân thanh toán thật — thà ra muộn còn hơn ra hỏng.',
      },
    ],
  },
  footer: {
    tagline: 'Game engine C++ mã nguồn mở, hướng dữ liệu.',
    product: 'Sản phẩm',
    community: 'Cộng đồng',
    documentation: 'Tài liệu',
    roadmap: 'Lộ trình',
    registry: 'Registry tài nguyên',
    discussions: 'Thảo luận',
  },
};

const DICTIONARIES: Record<Lang, Dictionary> = { en, vi };

export function getDictionary(lang: Lang): Dictionary {
  return DICTIONARIES[lang];
}
