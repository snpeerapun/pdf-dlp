import { useEffect, useRef, useState } from 'react'
import { Modal } from './Modal'
import { t } from '../i18n'
import { useStore, type DocState } from '../lib/store'
import { useOps } from '../lib/ops'
import { basename, dirname, fileSize, imagesToPdf, pickFolder, pickImages, pickPdfs, splitPdf, stem, writeFileB64, type Op } from '../lib/backend'
import { renderToDataUrl } from '../lib/pdf'
import { ensureFonts, textPng, watermarkPng, imageFileToPng } from '../lib/stampgen'
import { readFile } from '../lib/backend'

export type ToolKey = 'merge' | 'split' | 'insert' | 'rotate' | 'rotateL' | 'delete' | 'extract' | 'encrypt' | 'decrypt' | 'redact' | 'watermark' | 'pagenum' | 'crop' | 'images' | 'img2pdf' | 'compress' | 'sanitize' | 'props' | 'print'

const fmtBytes = (n: number) => n > 1048576 ? `${(n / 1048576).toFixed(2)} MB` : `${Math.round(n / 1024)} KB`

export function ToolModals({ tool, doc, onClose }: { tool: ToolKey | null; doc: DocState | null; onClose: () => void }) {
  const { state, toast, dispatch } = useStore()
  const ops = useOps()
  if (!tool) return null
  const close = onClose
  const sel = doc ? ops.pagesOrSel(doc) : []
  const selText = () => (state.selection.length ? `${state.selection.length} ${t('selected')}` : `${t('page')} ${state.page}`)

  // ---------- encrypt ----------
  if (tool === 'encrypt' && doc) return <EncryptModal doc={doc} onClose={close} />
  if (tool === 'decrypt' && doc) return <DecryptModal doc={doc} onClose={close} />
  if (tool === 'split' && doc) return <SplitModal doc={doc} onClose={close} />
  if (tool === 'merge') return <MergeModal doc={doc} onClose={close} />
  if (tool === 'insert' && doc) return <InsertModal doc={doc} onClose={close} />
  if (tool === 'watermark' && doc) return <WatermarkModal doc={doc} onClose={close} />
  if (tool === 'pagenum' && doc) return <PageNumModal doc={doc} onClose={close} />
  if (tool === 'crop' && doc) return <CropModal doc={doc} onClose={close} sel={sel} selText={selText()} />
  if (tool === 'images' && doc) return <ExportImagesModal doc={doc} onClose={close} />
  if (tool === 'img2pdf') return <Img2PdfModal onClose={close} />
  if (tool === 'compress' && doc) return <CompressModal doc={doc} onClose={close} />
  if (tool === 'sanitize' && doc) return <SanitizeModal doc={doc} onClose={close} />
  if (tool === 'props' && doc) return <PropsModal doc={doc} onClose={close} />
  if (tool === 'redact' && doc) return <RedactModal doc={doc} onClose={close} page={state.page} />
  if (tool === 'print' && doc) { void printDoc(doc, toast); close(); return null }
  void dispatch
  return null
}

function EncryptModal({ doc, onClose }: { doc: DocState; onClose: () => void }) {
  const ops = useOps(); const { toast } = useStore()
  const [user, setUser] = useState(''); const [owner, setOwner] = useState(''); const [p, setP] = useState({ print: true, copy: false, modify: false, annotate: false })
  const go = async () => {
    if (!user && !owner) { toast('warn', t('password')); return }
    await ops.run(doc, [{ op: 'encrypt', user_password: user, owner_password: owner, allow_print: p.print, allow_copy: p.copy, allow_modify: p.modify, allow_annotate: p.annotate }], { nextPassword: user || owner, busy: t('encrypt') })
    toast('ok', `${t('encrypt')} AES-256 ✓`); onClose()
  }
  return (
    <Modal title={t('encrypt')} icon="fa-lock" onClose={onClose} footer={<button className="btn primary" onClick={go}><i className="fa-solid fa-lock" /> {t('apply')}</button>}>
      <div className="grid2">
        <div className="field"><label>{t('userPassword')}</label><input type="password" value={user} onChange={(e) => setUser(e.target.value)} autoFocus /></div>
        <div className="field"><label>{t('ownerPassword')}</label><input type="password" value={owner} onChange={(e) => setOwner(e.target.value)} placeholder={t('optional')} /></div>
      </div>
      <div className="hint">{t('encryptHint')}</div>
      <div className="grid2">
        {(['print', 'copy', 'modify', 'annotate'] as const).map((k) => <label key={k} className="check"><input type="checkbox" checked={p[k]} onChange={(e) => setP({ ...p, [k]: e.target.checked })} /> {t(('perm_' + k) as 'perm_print')}</label>)}
      </div>
    </Modal>
  )
}
function DecryptModal({ doc, onClose }: { doc: DocState; onClose: () => void }) {
  const ops = useOps(); const { toast } = useStore()
  const go = async () => { await ops.run(doc, [{ op: 'decrypt' }], { nextPassword: '', busy: t('decrypt') }); toast('ok', t('decrypt') + ' ✓'); onClose() }
  return (
    <Modal title={t('decrypt')} icon="fa-lock-open" onClose={onClose} footer={<button className="btn primary" onClick={go}>{t('apply')}</button>}>
      <div>{doc.info?.encrypted || doc.password ? t('decryptHint') : t('notEncrypted')}</div>
    </Modal>
  )
}
function SplitModal({ doc, onClose }: { doc: DocState; onClose: () => void }) {
  const { toast, dispatch } = useStore()
  const [mode, setMode] = useState<'ranges' | 'each' | 'every'>('ranges'); const [spec, setSpec] = useState('1-1'); const [every, setEvery] = useState(1)
  const go = async () => {
    const dir = await pickFolder(); if (!dir) return
    dispatch({ type: 'busy', busy: t('split') })
    try { const r = await splitPdf(doc.workPath, doc.password, mode, spec, every, dir); toast('ok', `${t('split')}: ${r.files.length} ${t('filesUnit')} → ${dir}`); onClose() }
    catch (e) { toast('err', String(e)) } finally { dispatch({ type: 'busy', busy: null }) }
  }
  return (
    <Modal title={t('split')} icon="fa-scissors" onClose={onClose} footer={<button className="btn primary" onClick={go}><i className="fa-solid fa-folder-open" /> {t('chooseFolderAndSplit')}</button>}>
      <div className="seg"><button className={mode === 'ranges' ? 'on' : ''} onClick={() => setMode('ranges')}>{t('splitRanges')}</button><button className={mode === 'each' ? 'on' : ''} onClick={() => setMode('each')}>{t('splitEach')}</button><button className={mode === 'every' ? 'on' : ''} onClick={() => setMode('every')}>{t('splitEvery')}</button></div>
      {mode === 'ranges' && <div className="field"><label>{t('splitRangesHint')} (1-{doc.pages})</label><input value={spec} onChange={(e) => setSpec(e.target.value)} placeholder="1-3, 4-6, 7-" /></div>}
      {mode === 'every' && <div className="field"><label>{t('pagesPerFile')}</label><input type="number" min={1} value={every} onChange={(e) => setEvery(Math.max(1, Number(e.target.value)))} /></div>}
      <div className="hint">{t('splitOut')}</div>
    </Modal>
  )
}
function MergeModal({ doc, onClose }: { doc: DocState | null; onClose: () => void }) {
  const { state, toast } = useStore(); const ops = useOps()
  const [items, setItems] = useState<{ path: string; name: string; password?: string }[]>(() => state.docs.map((d) => ({ path: d.workPath, name: d.name, password: d.password })))
  const add = async () => { const ps = await pickPdfs(true); setItems([...items, ...ps.map((p) => ({ path: p, name: basename(p) }))]) }
  const move = (i: number, d: number) => { const a = [...items]; const j = i + d; if (j < 0 || j >= a.length) return; [a[i], a[j]] = [a[j], a[i]]; setItems(a) }
  const go = async () => { if (items.length < 1) return; await ops.merge(items, (doc ? stem(doc.name) : 'merged') + '-merged.pdf'); toast('ok', t('merge') + ' ✓'); onClose() }
  return (
    <Modal title={t('merge')} icon="fa-object-group" onClose={onClose} footer={<button className="btn primary" disabled={items.length < 1} onClick={go}>{t('merge')} ({items.length})</button>}>
      <div className="hint">{t('mergeHint')}</div>
      <div className="label-list">
        {items.map((it, i) => (
          <div key={it.path + i} className="label-item" style={{ cursor: 'default' }}>
            <span className="muted mono">{i + 1}</span><span className="nm">{it.name}</span>
            <button className="btn ghost icon sm" onClick={() => move(i, -1)}><i className="fa-solid fa-arrow-up" /></button>
            <button className="btn ghost icon sm" onClick={() => move(i, 1)}><i className="fa-solid fa-arrow-down" /></button>
            <button className="btn ghost icon sm" onClick={() => setItems(items.filter((_, j) => j !== i))}><i className="fa-solid fa-xmark" /></button>
          </div>
        ))}
      </div>
      <div><button className="btn" onClick={add}><i className="fa-solid fa-plus" /> {t('addFiles')}</button></div>
    </Modal>
  )
}
function InsertModal({ doc, onClose }: { doc: DocState; onClose: () => void }) {
  const { state, toast } = useStore(); const ops = useOps()
  const [file, setFile] = useState<string | null>(null); const [after, setAfter] = useState(state.page)
  const go = async () => { if (!file) return; await ops.insertAt(doc, { path: file }, Math.min(Math.max(0, after), doc.pages)); toast('ok', t('insert') + ' ✓'); onClose() }
  return (
    <Modal title={t('insert')} icon="fa-file-circle-plus" onClose={onClose} footer={<button className="btn primary" disabled={!file} onClick={go}>{t('apply')}</button>}>
      <div className="row"><button className="btn" onClick={async () => { const p = await pickPdfs(false); if (p[0]) setFile(p[0]) }}><i className="fa-regular fa-folder-open" /> {t('chooseFile')}</button><span className="muted">{file ? basename(file) : ''}</span></div>
      <div className="field"><label>{t('insertAfter')} (0 = {t('atStart')})</label><input type="number" min={0} max={doc.pages} value={after} onChange={(e) => setAfter(Number(e.target.value))} /></div>
    </Modal>
  )
}
function WatermarkModal({ doc, onClose }: { doc: DocState; onClose: () => void }) {
  const ops = useOps(); const { toast, state } = useStore()
  const [text, setText] = useState('CONFIDENTIAL'); const [color, setColor] = useState('#d12d2d'); const [opacity, setOpacity] = useState(0.22); const [size, setSize] = useState(0.7); const [angle, setAngle] = useState(45); const [img, setImg] = useState<{ dataUrl: string; aspect: number } | null>(null); const [scope, setScope] = useState<'all' | 'sel'>('all')
  const go = async () => {
    await ensureFonts()
    const png = img || watermarkPng(text, color)
    const pages = scope === 'all' ? [] : ops.pagesOrSel(doc)
    await ops.run(doc, [{ op: 'stamp', pages, png_base64: png.dataUrl, x: 0.5, y: 0.5, w: size, opacity, rotate_deg: angle, anchor: 'center' }], { busy: t('watermark') })
    toast('ok', t('watermark') + ' ✓'); onClose()
  }
  return (
    <Modal title={t('watermark')} icon="fa-droplet" onClose={onClose} footer={<button className="btn primary" onClick={go}>{t('apply')}</button>}>
      <div className="row"><div className="seg"><button className={!img ? 'on' : ''} onClick={() => setImg(null)}>{t('text')}</button><button className={img ? 'on' : ''} onClick={async () => { const p = await pickImages(); if (p[0]) setImg(await imageFileToPng(p[0], readFile)) }}>{t('image')}</button></div></div>
      {!img && <div className="grid2"><div className="field"><label>{t('text')}</label><input value={text} onChange={(e) => setText(e.target.value)} /></div><div className="field"><label>{t('color')}</label><input value={color} onChange={(e) => setColor(e.target.value)} /></div></div>}
      <div className="grid2">
        <div className="field"><label>{t('opacity')}: {Math.round(opacity * 100)}%</label><input type="range" min={5} max={100} value={opacity * 100} onChange={(e) => setOpacity(Number(e.target.value) / 100)} /></div>
        <div className="field"><label>{t('sizePct')}: {Math.round(size * 100)}%</label><input type="range" min={10} max={100} value={size * 100} onChange={(e) => setSize(Number(e.target.value) / 100)} /></div>
        <div className="field"><label>{t('angle')}: {angle}°</label><input type="range" min={-90} max={90} value={angle} onChange={(e) => setAngle(Number(e.target.value))} /></div>
        <div className="field"><label>{t('pages')}</label><div className="seg"><button className={scope === 'all' ? 'on' : ''} onClick={() => setScope('all')}>{t('allPages')}</button><button className={scope === 'sel' ? 'on' : ''} onClick={() => setScope('sel')}>{state.selection.length ? `${state.selection.length} ${t('selected')}` : `${t('page')} ${state.page}`}</button></div></div>
      </div>
    </Modal>
  )
}
function PageNumModal({ doc, onClose }: { doc: DocState; onClose: () => void }) {
  const ops = useOps(); const { toast } = useStore()
  const [fmt, setFmt] = useState('{n} / {total}'); const [pos, setPos] = useState<'bc' | 'br' | 'bl' | 'tc' | 'tr' | 'tl'>('bc'); const [start, setStart] = useState(1); const [header, setHeader] = useState(''); const [footer, setFooter] = useState('')
  const go = async () => {
    await ensureFonts()
    const list: Op[] = []
    const place = (anchor: typeof pos, aspect: number, w: number): { x: number; y: number } => {
      const h = w * aspect
      const x = anchor.endsWith('c') ? 0.5 - w / 2 : anchor.endsWith('r') ? 0.97 - w : 0.03
      const y = anchor.startsWith('b') ? 0.985 - h * (1 / 1) : 0.015
      return { x, y }
    }
    for (let p = 1; p <= doc.pages; p++) {
      const txt = fmt.replace('{n}', String(p + start - 1)).replace('{total}', String(doc.pages + start - 1))
      const png = textPng(txt, '#222', 36)
      const w = Math.min(0.3, 0.012 * txt.length + 0.03)
      const { x, y } = place(pos, png.aspect, w)
      list.push({ op: 'stamp', pages: [p], png_base64: png.dataUrl, x, y, w, opacity: 1, rotate_deg: 0, anchor: 'topleft' })
    }
    if (header) { const png = textPng(header, '#333', 32); const w = Math.min(0.6, 0.011 * header.length + 0.03); list.push({ op: 'stamp', pages: [], png_base64: png.dataUrl, x: 0.5 - w / 2, y: 0.012, w, opacity: 1, rotate_deg: 0, anchor: 'topleft' }) }
    if (footer) { const png = textPng(footer, '#333', 32); const w = Math.min(0.6, 0.011 * footer.length + 0.03); list.push({ op: 'stamp', pages: [], png_base64: png.dataUrl, x: 0.5 - w / 2, y: 0.975 - w * png.aspect, w, opacity: 1, rotate_deg: 0, anchor: 'topleft' }) }
    await ops.run(doc, list, { busy: t('pageNumbers') }); toast('ok', t('pageNumbers') + ' ✓'); onClose()
  }
  return (
    <Modal title={t('pageNumbers')} icon="fa-list-ol" onClose={onClose} footer={<button className="btn primary" onClick={go}>{t('apply')}</button>}>
      <div className="grid2">
        <div className="field"><label>{t('format')}</label><input value={fmt} onChange={(e) => setFmt(e.target.value)} /></div>
        <div className="field"><label>{t('startAt')}</label><input type="number" value={start} onChange={(e) => setStart(Number(e.target.value) || 1)} /></div>
        <div className="field"><label>{t('position')}</label><select value={pos} onChange={(e) => setPos(e.target.value as typeof pos)}>{(['bc', 'br', 'bl', 'tc', 'tr', 'tl'] as const).map((k) => <option key={k} value={k}>{t(('pos_' + k) as 'pos_bc')}</option>)}</select></div>
      </div>
      <div className="grid2"><div className="field"><label>{t('headerText')}</label><input value={header} onChange={(e) => setHeader(e.target.value)} /></div><div className="field"><label>{t('footerText')}</label><input value={footer} onChange={(e) => setFooter(e.target.value)} /></div></div>
      <div className="hint">{t('markRemovableHint')}</div>
    </Modal>
  )
}
function CropModal({ doc, onClose, sel, selText }: { doc: DocState; onClose: () => void; sel: number[]; selText: string }) {
  const ops = useOps(); const { toast } = useStore()
  const [v, setV] = useState({ left: 5, top: 5, right: 5, bottom: 5 }); const [scope, setScope] = useState<'all' | 'sel'>('all')
  const go = async () => { await ops.run(doc, [{ op: 'crop', pages: scope === 'all' ? [] : sel, left: v.left / 100, top: v.top / 100, right: v.right / 100, bottom: v.bottom / 100 }], { busy: t('crop') }); toast('ok', t('crop') + ' ✓'); onClose() }
  return (
    <Modal title={t('crop')} icon="fa-crop-simple" onClose={onClose} footer={<button className="btn primary" onClick={go}>{t('apply')}</button>}>
      <div className="hint">{t('cropHint')}</div>
      <div className="grid2">{(['left', 'top', 'right', 'bottom'] as const).map((k) => <div key={k} className="field"><label>{t(('side_' + k) as 'side_left')} (%)</label><input type="number" min={0} max={45} value={v[k]} onChange={(e) => setV({ ...v, [k]: Math.min(45, Math.max(0, Number(e.target.value))) })} /></div>)}</div>
      <div className="seg"><button className={scope === 'all' ? 'on' : ''} onClick={() => setScope('all')}>{t('allPages')}</button><button className={scope === 'sel' ? 'on' : ''} onClick={() => setScope('sel')}>{selText}</button></div>
    </Modal>
  )
}
function ExportImagesModal({ doc, onClose }: { doc: DocState; onClose: () => void }) {
  const { toast, dispatch, state } = useStore()
  const [fmt, setFmt] = useState<'png' | 'jpeg'>('png'); const [dpi, setDpi] = useState(150); const [scope, setScope] = useState<'all' | 'sel'>('all')
  const go = async () => {
    const dir = await pickFolder(); if (!dir) return
    const pages = scope === 'all' ? Array.from({ length: doc.pages }, (_, i) => i + 1) : (state.selection.length ? state.selection : [state.page])
    dispatch({ type: 'busy', busy: t('exportImages') })
    try {
      for (const p of pages) {
        const page = await doc.pdf.getPage(p)
        const widthPx = page.getViewport({ scale: 1 }).width * dpi / 72
        const r = await renderToDataUrl(page, widthPx, fmt === 'png' ? 'image/png' : 'image/jpeg', 0.92)
        await writeFileB64(`${dir}/${stem(doc.name)}-p${String(p).padStart(String(doc.pages).length, '0')}.${fmt === 'png' ? 'png' : 'jpg'}`, r.dataUrl)
      }
      toast('ok', `${t('exportImages')}: ${pages.length} → ${dir}`); onClose()
    } catch (e) { toast('err', String(e)) } finally { dispatch({ type: 'busy', busy: null }) }
  }
  return (
    <Modal title={t('exportImages')} icon="fa-images" onClose={onClose} footer={<button className="btn primary" onClick={go}><i className="fa-solid fa-folder-open" /> {t('chooseFolder')}</button>}>
      <div className="grid2">
        <div className="field"><label>{t('format')}</label><div className="seg"><button className={fmt === 'png' ? 'on' : ''} onClick={() => setFmt('png')}>PNG</button><button className={fmt === 'jpeg' ? 'on' : ''} onClick={() => setFmt('jpeg')}>JPEG</button></div></div>
        <div className="field"><label>DPI</label><select value={dpi} onChange={(e) => setDpi(Number(e.target.value))}>{[72, 110, 150, 200, 300].map((d) => <option key={d} value={d}>{d}</option>)}</select></div>
      </div>
      <div className="seg"><button className={scope === 'all' ? 'on' : ''} onClick={() => setScope('all')}>{t('allPages')}</button><button className={scope === 'sel' ? 'on' : ''} onClick={() => setScope('sel')}>{state.selection.length ? `${state.selection.length} ${t('selected')}` : `${t('page')} ${state.page}`}</button></div>
    </Modal>
  )
}
function Img2PdfModal({ onClose }: { onClose: () => void }) {
  const { toast } = useStore(); const ops = useOps()
  const [files, setFiles] = useState<string[]>([]); const [a4, setA4] = useState(true)
  const go = async () => { const r = await imagesToPdf(files, a4); await ops.openProduced(r.path, 'images.pdf'); toast('ok', `${t('images2pdf')}: ${r.pages} ${t('pages')}`); onClose() }
  return (
    <Modal title={t('images2pdf')} icon="fa-file-image" onClose={onClose} footer={<button className="btn primary" disabled={!files.length} onClick={go}>{t('apply')}</button>}>
      <div className="row"><button className="btn" onClick={async () => setFiles([...files, ...(await pickImages())])}><i className="fa-solid fa-plus" /> {t('addImages')}</button><label className="check"><input type="checkbox" checked={a4} onChange={(e) => setA4(e.target.checked)} /> {t('fitA4')}</label></div>
      <div className="label-list">{files.map((f, i) => <div key={f + i} className="label-item" style={{ cursor: 'default' }}><span className="nm">{basename(f)}</span><button className="btn ghost icon sm" onClick={() => setFiles(files.filter((_, j) => j !== i))}><i className="fa-solid fa-xmark" /></button></div>)}</div>
    </Modal>
  )
}
function CompressModal({ doc, onClose }: { doc: DocState; onClose: () => void }) {
  const ops = useOps(); const { toast, dispatch } = useStore()
  const [mode, setMode] = useState<'lossless' | 'raster'>('lossless'); const [dpi, setDpi] = useState(110); const [size, setSize] = useState<number | null>(null)
  useEffect(() => { fileSize(doc.workPath).then(setSize).catch(() => {}) }, [doc.workPath])
  const go = async () => {
    dispatch({ type: 'busy', busy: t('compress') })
    try {
      const list: Op[] = []
      if (mode === 'raster') {
        for (let p = 1; p <= doc.pages; p++) {
          const page = await doc.pdf.getPage(p); const vp = page.getViewport({ scale: 1 })
          const r = await renderToDataUrl(page, vp.width * dpi / 72, 'image/jpeg', 0.8)
          list.push({ op: 'redact', page: p, jpeg_base64: r.dataUrl, width_pt: vp.width, height_pt: vp.height })
        }
      }
      const res = await ops.run(doc, list, { busy: t('compress') })
      const after = await fileSize(res.path)
      toast('ok', `${t('compress')}: ${size ? fmtBytes(size) : '?'} → ${fmtBytes(after)}`); onClose()
    } catch (e) { toast('err', String(e)) } finally { dispatch({ type: 'busy', busy: null }) }
  }
  return (
    <Modal title={t('compress')} icon="fa-compress" onClose={onClose} footer={<button className="btn primary" onClick={go}>{t('apply')}</button>}>
      <div className="muted">{t('currentSize')}: {size ? fmtBytes(size) : '…'}</div>
      <div className="label-list">
        <div className={'label-item' + (mode === 'lossless' ? ' on' : '')} onClick={() => setMode('lossless')}><span className="nm">{t('compressLossless')}</span><span className="tip">{t('compressLosslessHint')}</span></div>
        <div className={'label-item' + (mode === 'raster' ? ' on' : '')} onClick={() => setMode('raster')}><span className="nm">{t('compressRaster')}</span><span className="tip">{t('compressRasterHint')}</span></div>
      </div>
      {mode === 'raster' && <div className="field"><label>DPI</label><select value={dpi} onChange={(e) => setDpi(Number(e.target.value))}>{[72, 96, 110, 150].map((d) => <option key={d} value={d}>{d}</option>)}</select></div>}
    </Modal>
  )
}
function SanitizeModal({ doc, onClose }: { doc: DocState; onClose: () => void }) {
  const ops = useOps(); const { toast } = useStore()
  const [keep, setKeep] = useState(false)
  const go = async () => { await ops.run(doc, [{ op: 'sanitize', keep_annotations: keep }], { busy: t('sanitize') }); toast('ok', t('sanitize') + ' ✓'); onClose() }
  return (
    <Modal title={t('sanitize')} icon="fa-broom" onClose={onClose} footer={<button className="btn primary" onClick={go}>{t('apply')}</button>}>
      <div>{t('sanitizeHint')}</div>
      <label className="check"><input type="checkbox" checked={keep} onChange={(e) => setKeep(e.target.checked)} /> {t('keepAnnotations')}</label>
    </Modal>
  )
}
function PropsModal({ doc, onClose }: { doc: DocState; onClose: () => void }) {
  const ops = useOps(); const { toast } = useStore()
  const m = doc.info?.metadata || {}
  const [v, setV] = useState({ Title: m.Title || '', Author: m.Author || '', Subject: m.Subject || '', Keywords: m.Keywords || '' })
  const go = async () => { await ops.run(doc, [{ op: 'metadata', title: v.Title, subject: v.Subject, keywords: v.Keywords, extra: { Author: v.Author } }], { busy: t('properties') }); toast('ok', t('saved')); onClose() }
  return (
    <Modal title={t('properties')} icon="fa-pen-to-square" onClose={onClose} footer={<button className="btn primary" onClick={go}>{t('save')}</button>}>
      {(['Title', 'Author', 'Subject', 'Keywords'] as const).map((k) => <div key={k} className="field"><label>{t(k.toLowerCase() as 'title')}</label><input value={v[k]} onChange={(e) => setV({ ...v, [k]: e.target.value })} /></div>)}
    </Modal>
  )
}

/** Redaction: draw black boxes on the current page; the page is replaced by a flattened image (text underneath is gone). */
function RedactModal({ doc, onClose, page }: { doc: DocState; onClose: () => void; page: number }) {
  const ops = useOps(); const { toast } = useStore()
  const canvasRef = useRef<HTMLCanvasElement>(null); const [boxes, setBoxes] = useState<{ x: number; y: number; w: number; h: number }[]>([]); const [drag, setDrag] = useState<{ x: number; y: number } | null>(null); const [cur, setCur] = useState<{ x: number; y: number } | null>(null); const [dpi, setDpi] = useState(200)
  const W = 640
  useEffect(() => { (async () => { const p = await doc.pdf.getPage(page); const r = await renderToDataUrl(p, W * 2, 'image/png'); const img = new Image(); img.onload = () => { const c = canvasRef.current; if (!c) return; c.width = r.width; c.height = r.height; c.getContext('2d')!.drawImage(img, 0, 0) }; img.src = r.dataUrl })() }, [doc.pdf, page])
  const norm = (e: React.MouseEvent) => { const r = (e.target as HTMLElement).getBoundingClientRect(); return { x: (e.clientX - r.left) / r.width, y: (e.clientY - r.top) / r.height } }
  const go = async () => {
    const p = await doc.pdf.getPage(page); const vp = page ? p.getViewport({ scale: 1 }) : null
    const r = await renderToDataUrl(p, vp!.width * dpi / 72, 'image/jpeg', 0.92, (ctx, w, h) => { ctx.fillStyle = '#000'; for (const b of boxes) ctx.fillRect(b.x * w, b.y * h, b.w * w, b.h * h) })
    await ops.run(doc, [{ op: 'redact', page, jpeg_base64: r.dataUrl, width_pt: vp!.width, height_pt: vp!.height }], { busy: t('redact') })
    toast('ok', `${t('redact')} ${t('page')} ${page} ✓`); onClose()
  }
  const live = drag && cur ? { x: Math.min(drag.x, cur.x), y: Math.min(drag.y, cur.y), w: Math.abs(cur.x - drag.x), h: Math.abs(cur.y - drag.y) } : null
  return (
    <Modal title={`${t('redact')} — ${t('page')} ${page}`} icon="fa-marker" onClose={onClose} wide footer={<><span className="muted" style={{ marginRight: 'auto' }}>{t('redactHint')}</span><select className="input" style={{ width: 110 }} value={dpi} onChange={(e) => setDpi(Number(e.target.value))}>{[110, 150, 200, 300].map((d) => <option key={d} value={d}>{d} dpi</option>)}</select><button className="btn ghost" onClick={() => setBoxes([])}>{t('clearBoxes')}</button><button className="btn danger" disabled={!boxes.length} onClick={go}><i className="fa-solid fa-marker" /> {t('applyRedact')} ({boxes.length})</button></>}>
      <div style={{ position: 'relative', width: W, margin: '0 auto', cursor: 'crosshair', userSelect: 'none' }}
        onMouseDown={(e) => { setDrag(norm(e)); setCur(norm(e)) }} onMouseMove={(e) => { if (drag) setCur(norm(e)) }}
        onMouseUp={() => { if (live && live.w > 0.005 && live.h > 0.005) setBoxes([...boxes, live]); setDrag(null); setCur(null) }}>
        <canvas ref={canvasRef} style={{ width: W, display: 'block', background: '#fff', pointerEvents: 'none' }} />
        {[...boxes, ...(live ? [live] : [])].map((b, i) => <div key={i} style={{ position: 'absolute', left: `${b.x * 100}%`, top: `${b.y * 100}%`, width: `${b.w * 100}%`, height: `${b.h * 100}%`, background: 'rgba(0,0,0,.85)', outline: '1px solid #ef5b5b', pointerEvents: 'none' }} />)}
      </div>
    </Modal>
  )
}

async function printDoc(doc: DocState, toast: (k: 'ok' | 'err' | 'warn' | 'info', s: string) => void) {
  try {
    const frame = document.createElement('iframe'); frame.style.position = 'fixed'; frame.style.right = '0'; frame.style.bottom = '0'; frame.style.width = '0'; frame.style.height = '0'; frame.style.border = '0'
    document.body.appendChild(frame)
    const d = frame.contentDocument!; d.open(); d.write('<!doctype html><html><head><style>@page{margin:0}body{margin:0}img{display:block;width:100%;page-break-after:always}</style></head><body></body></html>'); d.close()
    for (let p = 1; p <= doc.pages; p++) { const page = await doc.pdf.getPage(p); const r = await renderToDataUrl(page, page.getViewport({ scale: 1 }).width * 2, 'image/jpeg', 0.92); const img = d.createElement('img'); img.src = r.dataUrl; d.body.appendChild(img) }
    await new Promise((r) => setTimeout(r, 300))
    frame.contentWindow!.focus(); frame.contentWindow!.print()
    setTimeout(() => frame.remove(), 60000)
  } catch (e) { toast('err', String(e)) }
}
export { dirname }
