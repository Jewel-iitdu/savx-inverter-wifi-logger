import type { ComponentChildren } from 'preact';
import { useEffect } from 'preact/hooks';
import { t, type Lang } from '../lib/i18n';

export function Card(props: { title?: ComponentChildren; right?: ComponentChildren; children: ComponentChildren; class?: string }) {
  return (
    <section class={`card ${props.class ?? ''}`}>
      {(props.title || props.right) && (
        <header class="card-h">
          <h2>{props.title}</h2>
          {props.right && <div class="card-r">{props.right}</div>}
        </header>
      )}
      {props.children}
    </section>
  );
}

export function Tile(props: { label: string; value: ComponentChildren; sub?: ComponentChildren; tone?: string }) {
  return (
    <div class={`tile ${props.tone ? 'tone-' + props.tone : ''}`}>
      <div class="tile-l">{props.label}</div>
      <div class="tile-v">{props.value}</div>
      {props.sub !== undefined && <div class="tile-s">{props.sub}</div>}
    </div>
  );
}

export function KV(props: { rows: Array<[string, ComponentChildren]> }) {
  return (
    <dl class="kv">
      {props.rows.map(([k, v]) => (
        <div class="kv-r" key={k}>
          <dt>{k}</dt>
          <dd>{v}</dd>
        </div>
      ))}
    </dl>
  );
}

export function Banner(props: { tone: 'bad' | 'warn' | 'info' | 'ok'; children: ComponentChildren; action?: ComponentChildren }) {
  return (
    <div class={`banner banner-${props.tone}`} role={props.tone === 'bad' ? 'alert' : 'status'}>
      <div class="banner-t">{props.children}</div>
      {props.action}
    </div>
  );
}

export function Segmented<T extends string>(props: { value: T; options: Array<[T, string]>; onChange: (v: T) => void; label: string }) {
  return (
    <div class="seg" role="radiogroup" aria-label={props.label}>
      {props.options.map(([v, l]) => (
        <button
          type="button"
          role="radio"
          aria-checked={props.value === v}
          class={props.value === v ? 'on' : ''}
          onClick={() => props.onChange(v)}
          key={v}
        >
          {l}
        </button>
      ))}
    </div>
  );
}

export function Badge(props: { tone: string; children: ComponentChildren; plain?: boolean }) {
  return <span class={`badge tone-${props.tone}${props.plain ? ' plain' : ''}`}>{props.children}</span>;
}

export function Spinner(props: { label?: string }) {
  return (
    <div class="spinner" role="status">
      <span class="spin" aria-hidden="true" />
      {props.label ?? t('common.loading')}
    </div>
  );
}

export function Empty(props: { children: ComponentChildren }) {
  return <p class="empty">{props.children}</p>;
}

/** iOS-style on/off switch (a real checkbox, so it is keyboard- and VoiceOver-friendly). */
export function Switch(props: { checked: boolean; onChange: (v: boolean) => void; label: string; disabled?: boolean }) {
  return (
    <label class={`switch ${props.disabled ? 'dis' : ''}`} onClick={(e) => e.stopPropagation()}>
      <input
        type="checkbox"
        role="switch"
        checked={props.checked}
        disabled={props.disabled}
        aria-label={props.label}
        onChange={(e) => props.onChange(e.currentTarget.checked)}
      />
      <span class="switch-t" aria-hidden="true" />
    </label>
  );
}

/** A labelled row with a switch on the right. */
export function ToggleRow(props: { label: ComponentChildren; help?: ComponentChildren; checked: boolean; onChange: (v: boolean) => void; disabled?: boolean; aria: string }) {
  return (
    <div class="trow">
      <div class="trow-t">
        <span class="field-l">{props.label}</span>
        {props.help && <span class="field-h muted">{props.help}</span>}
      </div>
      <Switch checked={props.checked} onChange={props.onChange} label={props.aria} disabled={props.disabled} />
    </div>
  );
}

export function Chip(props: { on: boolean; onClick: () => void; children: ComponentChildren; title?: string; class?: string; disabled?: boolean }) {
  return (
    <button type="button" class={`chip ${props.on ? 'on' : ''} ${props.class ?? ''}`} aria-pressed={props.on} title={props.title} disabled={props.disabled} onClick={props.onClick}>
      {props.children}
    </button>
  );
}

/** Full-screen sheet above the tab bar (rule editor, preset picker). Locks the page scroll while open. */
export function Sheet(props: { title: string; onClose: () => void; children: ComponentChildren; footer?: ComponentChildren; closeLabel?: string }) {
  useEffect(() => {
    document.body.classList.add('sheet-open');
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') props.onClose();
    };
    window.addEventListener('keydown', onKey);
    return () => {
      document.body.classList.remove('sheet-open');
      window.removeEventListener('keydown', onKey);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);
  return (
    <div class="sheet" role="dialog" aria-modal="true" aria-label={props.title}>
      <header class="sheet-h">
        <button type="button" class="btn-link" onClick={props.onClose}>
          {props.closeLabel ?? t('common.cancel')}
        </button>
        <h2>{props.title}</h2>
        <span class="sheet-h-pad" />
      </header>
      <div class="sheet-b">{props.children}</div>
      {props.footer && <footer class="sheet-f">{props.footer}</footer>}
    </div>
  );
}

/** Header quick switch: shows the language you switch TO (বাং while English is on, EN while Bangla is on). */
export function LangButton({ lang, setLang, class: cls = 'lang-btn', full }: { lang: Lang; setLang: (l: Lang) => void; class?: string; full?: boolean }) {
  const next: Lang = lang === 'en' ? 'bn' : 'en';
  const label = next === 'bn' ? 'বাংলায় দেখুন' : 'Switch to English';
  return (
    <button type="button" class={cls} lang={next} aria-label={label} title={label} onClick={() => setLang(next)}>
      {next === 'bn' ? (full ? 'বাংলা' : 'বাং') : full ? 'English' : 'EN'}
    </button>
  );
}
