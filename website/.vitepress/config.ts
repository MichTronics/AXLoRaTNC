import { defineConfig } from 'vitepress'

export default defineConfig({
  title: 'AXLoRaTNC',
  description: 'AX.25 packet-radio TNC for ESP32 + LoRa',
  base: '/AXLoRaTNC/',

  // /flash/ is a static HTML file in public/ — not a VitePress page.
  ignoreDeadLinks: [/\/flash\//],

  head: [
    ['link', { rel: 'icon', href: '/AXLoRaTNC/favicon.svg' }],
  ],

  themeConfig: {
    logo: { light: '/logo-light.svg', dark: '/logo-dark.svg', alt: 'AXLoRaTNC' },

    nav: [
      { text: 'Guide',   link: '/guide/' },
      { text: 'Flash',   link: '/flash/', target: '_self' },
      { text: 'GitHub',  link: 'https://github.com/MichTronics/AXLoRaTNC' },
      { text: 'Releases',link: 'https://github.com/MichTronics/AXLoRaTNC/releases' },
    ],

    sidebar: [
      {
        text: 'Guide',
        items: [
          { text: 'Getting started',    link: '/guide/' },
          { text: 'Radio config',       link: '/guide/radio-config' },
          { text: 'KISS mode',          link: '/guide/kiss' },
          { text: 'LinFBB / F6FBB KISS', link: '/guide/fbb-kiss' },
          { text: 'WA8DED hostmode',    link: '/guide/wa8ded' },
          { text: 'APRS',               link: '/guide/aprs' },
          { text: 'NET/ROM & BBS',      link: '/guide/netrom-bbs' },
          { text: 'BPQ / LinBPQ setup', link: '/guide/bpq' },
        ],
      },
    ],

    socialLinks: [
      { icon: 'github', link: 'https://github.com/MichTronics/AXLoRaTNC' },
    ],

    footer: {
      message: 'Released under the MIT License.',
      copyright: 'AXLoRaTNC – MichTronics',
    },

    search: { provider: 'local' },
  },
})
