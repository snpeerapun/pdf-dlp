// Dev-only end-to-end self test running inside the real Tauri runtime.
// Trigger: write {"fixture": "/abs/file.pdf", "out": "/tmp/dir"} to /tmp/pdfdlp-selftest.json while `tauri dev` runs.
import { invoke } from '@tauri-apps/api/core'
import { applyOps, copyFile, pdfInfo, writeFileB64, mergePdfs, type Op } from './backend'
import { loadPdf, pageSizes, renderToDataUrl } from './pdf'
import { readFile } from './backend'
import { badgePng, ensureFonts, watermarkPng } from './stampgen'

type Step = { name: string; ok: boolean; ms: number; detail?: string }
export async function runSelfTest(cfg: { fixture: string; out: string }): Promise<Step[]> {
  const steps: Step[] = []
  const step = async (name: string, fn: () => Promise<string | void>) => {
    const t0 = performance.now()
    try { const d = await fn(); steps.push({ name, ok: true, ms: Math.round(performance.now() - t0), detail: d || undefined }) }
    catch (e) { steps.push({ name, ok: false, ms: Math.round(performance.now() - t0), detail: String(e) }) }
  }
  let work = cfg.fixture; let pw = ''
  await step('info', async () => { const i = await pdfInfo(work); if (i.pages < 1) throw new Error('no pages'); return `pages=${i.pages} enc=${i.encrypted}` })
  await step('render page1 via pdf.js', async () => { const b = await readFile(work); const pdf = await loadPdf(b); const sizes = await pageSizes(pdf); const p = await pdf.getPage(1); const r = await renderToDataUrl(p, 1200, 'image/png'); if (r.width !== 1200) throw new Error('bad width'); return `sizes=${sizes.length} px=${r.width}x${r.height}` })
  await step('rotate+delete', async () => { const r = await applyOps(work, pw, [{ op: 'rotate', pages: [1], angle: 90 }, { op: 'delete', pages: [3] }]); work = r.path; if (r.pages !== 2) throw new Error(`pages=${r.pages}`); return `pages=${r.pages}` })
  await step('classify (badge+watermark+msip)', async () => {
    await ensureFonts()
    const b = badgePng('CONFIDENTIAL', '#f4b63f'); const wm = watermarkPng('CONFIDENTIAL', '#f4b63f')
    const w = 0.3
    const ops: Op[] = [{ op: 'remove_marks' }, { op: 'stamp', pages: [], png_base64: b.dataUrl, x: 0.5 - w / 2, y: 0.012, w, opacity: 1, rotate_deg: 0, anchor: 'topleft' }, { op: 'stamp', pages: [], png_base64: wm.dataUrl, x: 0.5, y: 0.5, w: 0.75, opacity: 0.18, rotate_deg: 45, anchor: 'center' }, { op: 'metadata', classification: 'Confidential', keywords: 'Classification: Confidential', extra: { 'MSIP_Label_11111111-2222-3333-4444-555555555555_Name': 'Confidential', 'MSIP_Label_11111111-2222-3333-4444-555555555555_Enabled': 'true' } }]
    const r = await applyOps(work, pw, ops); work = r.path
    const i = await pdfInfo(work); if (i.classification !== 'Confidential' || !i.has_marks || i.msip?.name !== 'Confidential') throw new Error(JSON.stringify({ c: i.classification, m: i.has_marks, msip: i.msip }))
    return `msip=${i.msip?.id}`
  })
  await step('render classified page (visual check file)', async () => { const b = await readFile(work); const pdf = await loadPdf(b); const p = await pdf.getPage(1); const r = await renderToDataUrl(p, 900, 'image/png'); await writeFileB64(`${cfg.out}/selftest-classified-p1.png`, r.dataUrl); return 'written' })
  await step('encrypt AES-256', async () => { const r = await applyOps(work, pw, [{ op: 'encrypt', user_password: 'secret', owner_password: 'owner', allow_print: true, allow_copy: false, allow_modify: false, allow_annotate: false }]); work = r.path; pw = 'secret'; const i = await pdfInfo(work, 'secret'); if (!i.encrypted || i.pages !== 2) throw new Error(JSON.stringify(i)); const bad = await pdfInfo(work, 'nope'); if (!bad.needs_password) throw new Error('wrong password accepted'); return 'ok' })
  await step('pdf.js opens encrypted with password', async () => { const b = await readFile(work); const pdf = await loadPdf(b, 'secret'); return `pages=${pdf.numPages}` })
  await step('remove label + decrypt', async () => { const r = await applyOps(work, pw, [{ op: 'remove_marks' }, { op: 'metadata', classification: '', keywords: '', extra: { 'MSIP_Label_11111111-2222-3333-4444-555555555555_Name': '', 'MSIP_Label_11111111-2222-3333-4444-555555555555_Enabled': '' } }, { op: 'decrypt' }]); work = r.path; pw = ''; const i = await pdfInfo(work); if (i.encrypted || i.has_marks || i.classification) throw new Error(JSON.stringify(i)); return 'clean' })
  await step('merge with itself', async () => { const r = await mergePdfs([{ path: work }, { path: cfg.fixture, pages: [1] }]); if (r.pages !== 3) throw new Error(`pages=${r.pages}`); return `pages=${r.pages}` })
  await step('save as', async () => { await copyFile(work, `${cfg.out}/selftest-final.pdf`); return `${cfg.out}/selftest-final.pdf` })
  return steps
}

export function installSelfTestPoller() {
  if (!import.meta.env.DEV) return
  const tick = async () => {
    try {
      const raw = await invoke<string | null>('dev_selftest_trigger')
      if (!raw) return
      const cfg = JSON.parse(raw)
      console.log('[selftest] start', cfg)
      const steps = await runSelfTest(cfg)
      const json = JSON.stringify({ ok: steps.every((s) => s.ok), steps }, null, 1)
      console.log('[selftest] done ' + json)
      await writeFileB64(`${cfg.out}/selftest-result.json`, btoa(unescape(encodeURIComponent(json))))
    } catch (e) { console.error('[selftest] failed', e) }
  }
  setInterval(tick, 3000)
}
