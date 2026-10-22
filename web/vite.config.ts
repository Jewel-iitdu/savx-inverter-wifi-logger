import { defineConfig, type Plugin } from 'vite';
import { createHash } from 'node:crypto';
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('.', import.meta.url));
const pkg = JSON.parse(readFileSync(resolve(root, 'package.json'), 'utf8')) as { version: string };

/** Every servable file under public/ (icons, manifest), as root-relative URLs. */
function publicFiles(dir: string): string[] {
  const out: string[] = [];
  const walk = (d: string) => {
    for (const name of readdirSync(d)) {
      if (name.startsWith('.') || name.startsWith('_')) continue; // dotfiles, _headers, _redirects: host config, never served
      const p = join(d, name);
      if (statSync(p).isDirectory()) walk(p);
      else out.push('/' + relative(dir, p).split('\\').join('/'));
    }
  };
  walk(dir);
  return out;
}

/**
 * Emits dist/sw.js from sw.template.js with the precache list of this build
 * (hashed /assets/*, icons, manifest, `/`). A hand-written service
 * worker keeps the app shell offline-capable without Workbox (d1 §6.5).
 */
function serviceWorker(): Plugin {
  return {
    name: 'savx-service-worker',
    apply: 'build',
    generateBundle(_options, bundle) {
      const built = Object.keys(bundle)
        .filter((f) => !f.endsWith('.map') && f !== 'index.html' && f !== 'sw.js')
        .map((f) => '/' + f);
      // No '/index.html': Cloudflare answers it with 307 -> '/', and a precached redirect can break
      // offline launch (research/r7-web-hosting.md section 3.4). The SW falls back to the cached '/'.
      const urls = Array.from(new Set(['/', ...built, ...publicFiles(resolve(root, 'public'))])).sort();
      const version = createHash('sha256').update(urls.join('\n')).update(pkg.version).digest('hex').slice(0, 12);
      const template = readFileSync(resolve(root, 'sw.template.js'), 'utf8');
      const source = template
        .replace('__SW_VERSION__', version)
        .replace('/*__PRECACHE__*/ []', JSON.stringify(urls, null, 1));
      this.emitFile({ type: 'asset', fileName: 'sw.js', source });
    },
  };
}

export default defineConfig({
  // Preact via the automatic JSX runtime; no Babel preset needed.
  esbuild: { jsx: 'automatic', jsxImportSource: 'preact' },
  define: {
    __APP_VERSION__: JSON.stringify(pkg.version),
    __BUILD_TIME__: JSON.stringify(new Date().toISOString()),
  },
  plugins: [serviceWorker()],
  build: {
    target: ['es2020', 'safari15'],
    sourcemap: false,
    assetsInlineLimit: 0,
    reportCompressedSize: true,
    rollupOptions: {
      output: {
        // Separate long-lived vendor chunks so an app-only deploy does not
        // invalidate the cached Firebase SDK on the phone.
        manualChunks(id) {
          if (id.includes('node_modules/@firebase/') || id.includes('node_modules/firebase/')) return 'firebase';
          if (id.includes('node_modules/uplot/')) return 'uplot';
          if (id.includes('node_modules/preact/')) return 'preact';
          return undefined;
        },
      },
    },
  },
  server: { host: true, port: 5173 },
  preview: { host: true, port: 4173 },
});
