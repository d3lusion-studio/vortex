import { defineI18nUI } from 'fumadocs-ui/i18n';
import { i18n } from './i18n';

/**
 * Translations for Fumadocs' own chrome (search box, sidebar, "last updated", ...).
 * English ships with the library; Vietnamese is supplied here.
 */
export const { provider } = defineI18nUI(i18n, {
  en: {
    displayName: 'English',
  },
  vi: {
    displayName: 'Tiếng Việt',
    search: 'Tìm kiếm',
    searchNoResult: 'Không tìm thấy kết quả',
    toc: 'Nội dung trang',
    tocNoHeadings: 'Trang này không có mục lục',
    lastUpdate: 'Cập nhật lần cuối',
    chooseLanguage: 'Chọn ngôn ngữ',
    nextPage: 'Trang sau',
    previousPage: 'Trang trước',
    chooseTheme: 'Giao diện',
    editOnGithub: 'Sửa trên GitHub',
  },
});
