// Settings → Firmware update (fw 0.9.2): the installed version, the latest public GitHub release of the repo the
// logger reports (status.repo) and the logger's update state (status.ota). "Update" writes cfg {ota, otaAt}; the logger
// then downloads that release's OTA image itself, checks its SHA-256 against SHA256SUMS.txt, installs it and restarts.
// A new image that does not reach the cloud within 10 minutes is rolled back by the bootloader (net/OtaPlan.h).
import { useState } from 'preact/hooks';
import { useApp } from '../context';
import { useAsync } from '../lib/hooks';
import { t } from '../lib/i18n';
import type { Status } from '../data/types';
import { Banner, Card, KV } from './ui';

/** "0.9.3" / "v0.9.3" -> 903 (the firmware's ota::versionCode); 0 = not a version. */
export function versionCode(v: string | undefined | null): number {
  const m = /^v?(\d{1,2})\.(\d{1,2})\.(\d{1,2})$/.exec((v ?? '').trim());
  return m ? Number(m[1]) * 10000 + Number(m[2]) * 100 + Number(m[3]) : 0;
}

interface Release {
  tag_name: string;
  html_url: string;
}

const BUSY = /^(checking release|downloading|installed)/;

export function FirmwareCard({ status, fw }: { status: Status | null | undefined; fw: string | undefined }) {
  const { source } = useApp();
  const repo = status?.repo ?? '';
  const rel = useAsync<Release | null>(
    () => fetch(`https://api.github.com/repos/${repo}/releases/latest`).then((r) => (r.ok ? (r.json() as Promise<Release>) : null)),
    [repo],
    !!repo,
  );
  const [msg, setMsg] = useState<{ tone: 'ok' | 'bad'; text: string } | null>(null);
  const [saving, setSaving] = useState(false);
  const have = versionCode(fw);
  const latest = rel.data ? versionCode(rel.data.tag_name) : 0;
  const ver = rel.data?.tag_name.replace(/^v/, '') ?? '';
  const busy = BUSY.test(status?.ota ?? '');
  const newer = !!repo && latest > have && have > 0;

  const request = async () => {
    if (!window.confirm(t('fw.confirm', { v: ver }))) return;
    setSaving(true);
    setMsg(null);
    try {
      await source.setCfg({ ota: latest, otaAt: Date.now() });
      setMsg({ tone: 'ok', text: t('fw.requested') });
    } catch (e) {
      setMsg({ tone: 'bad', text: t('set.notSaved', { msg: e instanceof Error ? e.message : String(e) }) });
    } finally {
      setSaving(false);
    }
  };

  return (
    <Card title={t('fw.title')}>
      {!repo ? (
        <p class="muted small">{t('fw.off')}</p>
      ) : (
        <>
          <KV
            rows={[
              [t('fw.installed'), fw ?? '–'],
              [
                t('fw.latest'),
                rel.loading && !rel.data ? (
                  '…'
                ) : rel.data ? (
                  <a href={rel.data.html_url} target="_blank" rel="noopener">
                    {ver}
                  </a>
                ) : (
                  t('fw.noRelease')
                ),
              ],
              [t('fw.state'), status?.ota ?? '–'],
            ]}
          />
          {newer ? (
            <div class="actions">
              <button type="button" class="btn" disabled={saving || busy} onClick={() => void request()}>
                {t('fw.update', { v: ver })}
              </button>
            </div>
          ) : (
            rel.data && <p class="muted small">{t('fw.upToDate')}</p>
          )}
          {msg && <Banner tone={msg.tone}>{msg.text}</Banner>}
          <p class="muted small">{t('fw.note')}</p>
        </>
      )}
    </Card>
  );
}
