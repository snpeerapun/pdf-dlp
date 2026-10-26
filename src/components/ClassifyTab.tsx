import { useEffect, useState } from 'react'
import type { DocState } from '../lib/store'
import { useStore } from '../lib/store'
import { useOps } from '../lib/ops'
import { t } from '../i18n'
import { applyOps, copyFile, discoverLabels, labelsFromFile, msipMetadata, newGuid, pickFolder, pickPdfs, basename, type Label, type Op } from '../lib/backend'
import { contentBits, loadLabels, loadPrefs, mergeLabels, saveLabels, savePrefs, type LabelPrefs } from '../lib/labels'
import { badgePng, ensureFonts, watermarkPng } from '../lib/stampgen'
import { Modal } from './Modal'

export async function buildLabelOps(label: Label, prefs: LabelPrefs, previousMeta: Record<string, string>, encryptPassword?: string): Promise<Op[]> {
  await ensureFonts()
  const ops: Op[] = [{ op: 'remove_marks' }]
  const color = label.color || '#f4b63f'
  if (prefs.header || prefs.footer) {
    const b = badgePng(label.name.toUpperCase(), color)
    const w = Math.min(0.42, 0.012 * label.name.length + 0.06)
    if (prefs.header) ops.push({ op: 'stamp', pages: [], png_base64: b.dataUrl, x: 0.5 - w / 2, y: 0.012, w, opacity: 1, rotate_deg: 0, anchor: 'topleft' })
    if (prefs.footer) ops.push({ op: 'stamp', pages: [], png_base64: b.dataUrl, x: 0.5 - w / 2, y: 0.988 - w * b.aspect, w, opacity: 1, rotate_deg: 0, anchor: 'topleft' })
  }
  if (prefs.watermark) { const wm = watermarkPng(label.name.toUpperCase(), color); ops.push({ op: 'stamp', pages: [], png_base64: wm.dataUrl, x: 0.5, y: 0.5, w: 0.75, opacity: 0.18, rotate_deg: 45, anchor: 'center' }) }
  const extra: Record<string, string> = prefs.writeMsip ? await msipMetadata(label, prefs.siteId, contentBits(prefs), prefs.owner, previousMeta) : {}
  extra.ClassificationLevel = String(label.order + 1)
  extra.ClassifiedAt = new Date().toISOString()
  ops.push({ op: 'metadata', classification: label.name, keywords: `Classification: ${label.name}`, extra })
  if (prefs.encrypt && encryptPassword) ops.push({ op: 'encrypt', user_password: encryptPassword, owner_password: encryptPassword, allow_print: true, allow_copy: false, allow_modify: false, allow_annotate: false })
  return ops
}

export function ClassifyTab({ doc }: { doc: DocState | null }) {
  const { toast, dispatch } = useStore(); const ops = useOps()
  const [labels, setLabels] = useState<Label[]>(loadLabels); const [prefs, setPrefs] = useState<LabelPrefs>(loadPrefs)
  const [sel, setSel] = useState<string | null>(null); const [pw, setPw] = useState(''); const [manual, setManual] = useState<{ name: string; id: string; color: string } | null>(null); const [notes, setNotes] = useState<string[]>([])
  useEffect(() => saveLabels(labels), [labels]); useEffect(() => savePrefs(prefs), [prefs])
  const current = doc?.info?.classification || doc?.info?.msip?.name || null
  const curLabel = labels.find((l) => l.name === current || l.id === doc?.info?.msip?.id)

  const readWindows = async () => {
    dispatch({ type: 'busy', busy: t('readFromWindows') })
    try { const d = await discoverLabels(); setLabels(mergeLabels(labels, d.labels)); setNotes([`${t('scanned')}: ${d.scanned.length} · ${t('found')}: ${d.labels.length}`, ...d.notes]); toast(d.labels.length ? 'ok' : 'warn', `${t('found')} ${d.labels.length} label`) }
    catch (e) { toast('err', String(e)) } finally { dispatch({ type: 'busy', busy: null }) }
  }
  const learn = async () => {
    const ps = await pickPdfs(true); if (!ps.length) return
    let n = 0
    for (const p of ps) { try { const ls = await labelsFromFile(p); n += ls.length; setLabels((cur) => mergeLabels(cur, ls)) } catch { /* skip */ } }
    toast(n ? 'ok' : 'warn', `${t('found')} ${n} label`)
  }
  const apply = async () => {
    const label = labels.find((l) => l.id === sel); if (!doc || !label) return
    if (prefs.encrypt && !pw) { toast('warn', t('password')); return }
    const list = await buildLabelOps(label, prefs, doc.info?.metadata || {}, pw)
    await ops.run(doc, list, { busy: t('applyLabel'), nextPassword: prefs.encrypt ? pw : undefined })
    toast('ok', `${t('labelApplied')}: ${label.name}`)
  }
  const remove = async () => {
    if (!doc) return
    const extra = Object.fromEntries(Object.keys(doc.info?.metadata || {}).filter((k) => k.startsWith('MSIP_Label_') || ['ClassificationLevel', 'ClassifiedAt'].includes(k)).map((k) => [k, '']))
    await ops.run(doc, [{ op: 'remove_marks' }, { op: 'metadata', classification: '', keywords: '', extra }], { busy: t('removeLabel') })
    toast('ok', t('labelRemoved'))
  }
  const batch = async () => {
    const label = labels.find((l) => l.id === sel); if (!label) return
    const files = await pickPdfs(true); if (!files.length) return
    const dir = await pickFolder(); if (!dir) return
    dispatch({ type: 'busy', busy: `${t('batch')} 0/${files.length}` })
    let ok = 0
    try {
      for (const [i, f] of files.entries()) {
        dispatch({ type: 'busy', busy: `${t('batch')} ${i + 1}/${files.length}` })
        try { const list = await buildLabelOps(label, prefs, {}, pw); const r = await applyOps(f, '', list); await copyFile(r.path, `${dir}/${basename(f)}`); ok++ } catch (e) { toast('err', `${basename(f)}: ${String(e)}`) }
      }
      toast('ok', `${t('batch')} ${t('batchDone')}: ${ok}/${files.length} → ${dir}`)
    } finally { dispatch({ type: 'busy', busy: null }) }
  }

  return (
    <>
      <div className="card">
        <h4><i className="fa-solid fa-tag" /> {t('currentLabel')}</h4>
        <div className="row">
          {current ? <span className="badge dot" style={{ background: (curLabel?.color || '#f4b63f') + '33', color: curLabel?.color || '#ffd27a' }}>{current}</span> : <span className="badge">{t('noLabel')}</span>}
          {doc?.info?.msip && <span className="badge info">MSIP</span>}
          {doc?.info?.has_marks && <span className="badge">{t('marks')}</span>}
          <span className="spacer" />
          {doc && (current || doc.info?.has_marks) && <button className="btn sm ghost" onClick={remove}><i className="fa-solid fa-eraser" /> {t('removeLabel')}</button>}
        </div>
      </div>
      <div className="card">
        <h4><i className="fa-solid fa-list-check" /> {t('chooseLabel')}</h4>
        {labels.length === 0 && <div className="hint">{t('noLabels')}</div>}
        <div className="label-list">
          {labels.map((l) => (
            <div key={l.id} className={'label-item' + (sel === l.id ? ' on' : '')} onClick={() => setSel(l.id)} title={l.source}>
              <span className="sw" style={{ background: l.color || '#888' }} /><span className="nm">{l.name}</span>
              {l.id.length === 36 && <span className="badge" style={{ height: 18, fontSize: 10 }}>MSIP</span>}
              <button className="btn ghost icon sm" title={t('deleteLabel')} onClick={(e) => { e.stopPropagation(); setLabels(labels.filter((x) => x.id !== l.id)); if (sel === l.id) setSel(null) }}><i className="fa-solid fa-xmark" /></button>
            </div>
          ))}
        </div>
        <div className="row">
          <button className="btn sm" onClick={readWindows}><i className="fa-brands fa-windows" /> {t('readFromWindows')}</button>
          <button className="btn sm" onClick={learn}><i className="fa-solid fa-graduation-cap" /> {t('learnFromFile')}</button>
          <button className="btn sm ghost" onClick={async () => setManual({ name: '', id: await newGuid(), color: '#f4b63f' })}><i className="fa-solid fa-plus" /> {t('addManual')}</button>
        </div>
        {notes.length > 0 && <div className="hint">{notes.map((n, i) => <div key={i}>{n}</div>)}</div>}
      </div>
      <div className="card">
        <h4><i className="fa-solid fa-stamp" /> {t('marking')}</h4>
        <label className="check"><input type="checkbox" checked={prefs.header} onChange={(e) => setPrefs({ ...prefs, header: e.target.checked })} /> {t('markHeader')}</label>
        <label className="check"><input type="checkbox" checked={prefs.footer} onChange={(e) => setPrefs({ ...prefs, footer: e.target.checked })} /> {t('markFooter')}</label>
        <label className="check"><input type="checkbox" checked={prefs.watermark} onChange={(e) => setPrefs({ ...prefs, watermark: e.target.checked })} /> {t('markWatermark')}</label>
        <label className="check"><input type="checkbox" checked={prefs.writeMsip} onChange={(e) => setPrefs({ ...prefs, writeMsip: e.target.checked })} /> {t('msipWrite')}</label>
        {prefs.writeMsip && <div className="field"><label>{t('siteId')}</label><input value={prefs.siteId} onChange={(e) => setPrefs({ ...prefs, siteId: e.target.value })} placeholder="00000000-0000-0000-0000-000000000000" /></div>}
        <label className="check"><input type="checkbox" checked={prefs.encrypt} onChange={(e) => setPrefs({ ...prefs, encrypt: e.target.checked })} /> {t('markEncrypt')}</label>
        {prefs.encrypt && <div className="field"><input type="password" value={pw} onChange={(e) => setPw(e.target.value)} placeholder={t('password')} /></div>}
        <div className="row">
          <button className="btn primary" disabled={!doc || !sel} onClick={apply}><i className="fa-solid fa-tag" /> {t('applyLabel')}</button>
          <button className="btn" disabled={!sel} onClick={batch} title={t('batchHint')}><i className="fa-solid fa-layer-group" /> {t('batch')}</button>
        </div>
      </div>
      {manual && (
        <Modal title={t('addManual')} icon="fa-tag" onClose={() => setManual(null)} footer={<button className="btn primary" disabled={!manual.name.trim()} onClick={() => { setLabels(mergeLabels(labels, [{ id: manual.id, name: manual.name.trim(), tooltip: '', color: manual.color, parent: '', order: labels.length, source: 'manual' }])); setManual(null) }}>{t('ok')}</button>}>
          <div className="field"><label>{t('labelName')}</label><input autoFocus value={manual.name} onChange={(e) => setManual({ ...manual, name: e.target.value })} placeholder="Confidential" /></div>
          <div className="field"><label>{t('labelGuid')}</label><div className="row"><input className="input mono" value={manual.id} onChange={(e) => setManual({ ...manual, id: e.target.value.trim().toLowerCase() })} /><button className="btn sm" onClick={async () => setManual({ ...manual, id: await newGuid() })}>{t('generate')}</button></div></div>
          <div className="field"><label>{t('labelColor')}</label><input value={manual.color} onChange={(e) => setManual({ ...manual, color: e.target.value })} /></div>
        </Modal>
      )}
    </>
  )
}
