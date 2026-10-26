import { useCallback, useEffect, useReducer, useRef, useState } from 'react'
import { StoreProvider, useStore, type DocState } from './lib/store'
import { loadPdf, NeedsPassword, pageSizes } from './lib/pdf'
import { basename, copyFile, isTauri, onDrop, onOpenFiles, pdfInfo, pickPdfs, pickSavePath, readFile, takePending } from './lib/backend'
import { t } from './i18n'
import { Topbar } from './components/Topbar'
import { Sidebar } from './components/Sidebar'
import { Viewer } from './components/Viewer'
import { Panel } from './components/Panel'
import { Modal } from './components/Modal'
import { Toasts } from './components/Toasts'
import { SettingsModal } from './components/SettingsModal'
import type { MenuItem } from './components/ContextMenu'
import { ToolModals, type ToolKey } from './components/ToolModals'
import { useOps } from './lib/ops'
import { installSelfTestPoller } from './lib/selftest'

let seq = 0

function Shell() {
  const { state, dispatch, toast } = useStore()
  const [, rerender] = useReducer((x: number) => x + 1, 0)
  const [pw, setPw] = useState<{ path: string; wrong: boolean; resolve: (v: string | null) => void } | null>(null)
  const [settings, setSettings] = useState(false)
  const pwInput = useRef<HTMLInputElement>(null)
  const doc = state.docs.find((d) => d.id === state.activeId) || null
  const ops = useOps()
  const [tool, setTool] = useState<ToolKey | null>(null)
  useEffect(() => {
    const h = async (e: Event) => {
      const k = (e as CustomEvent<ToolKey>).detail
      if (!doc && !['merge', 'img2pdf'].includes(k)) return
      try {
        if (k === 'rotate' && doc) await ops.rotate(doc, 90)
        else if (k === 'rotateL' && doc) await ops.rotate(doc, -90)
        else if (k === 'delete' && doc) { const ps = ops.pagesOrSel(doc); if (ps.length >= doc.pages) { toast('warn', t('error')); return } await ops.deletePages(doc, ps) }
        else if (k === 'extract' && doc) await ops.extract(doc)
        else setTool(k)
      } catch { /* toast already shown */ }
    }
    window.addEventListener('tool', h); return () => window.removeEventListener('tool', h)
  }, [doc, ops, toast])
  useEffect(() => {
    const key = (e: KeyboardEvent) => { if ((e.metaKey || e.ctrlKey) && e.key === 'z' && doc?.history.length) { e.preventDefault(); void ops.undo(doc) } if ((e.metaKey || e.ctrlKey) && e.key === 'o') { e.preventDefault(); void onOpen() } }
    const u = () => { if (doc?.history.length) void ops.undo(doc) }
    window.addEventListener('keydown', key); window.addEventListener('undo', u); return () => { window.removeEventListener('keydown', key); window.removeEventListener('undo', u) }
  })

  const askPassword = (path: string, wrong: boolean) => new Promise<string | null>((resolve) => setPw({ path, wrong, resolve }))

  const openPath = useCallback(async (path: string): Promise<DocState | null> => {
    if (state.docs.some((d) => d.path === path)) { const d = state.docs.find((x) => x.path === path)!; dispatch({ type: 'active', id: d.id }); return d }
    dispatch({ type: 'busy', busy: t('loading') })
    try {
      const bytes = await readFile(path)
      let password = ''
      let pdf = null as Awaited<ReturnType<typeof loadPdf>> | null
      let wrong = false
      for (let i = 0; i < 5 && !pdf; i++) {
        try { pdf = await loadPdf(bytes, password) } catch (e) {
          if (e instanceof NeedsPassword) { const p = await askPassword(path, wrong || e.wrong); if (p === null) return null; password = p; wrong = true; continue }
          throw e
        }
      }
      if (!pdf) return null
      const sizes = await pageSizes(pdf)
      const info = await pdfInfo(path, password).catch(() => null)
      const d: DocState = { id: 'd' + ++seq, name: basename(path), path, workPath: path, password, pdf, pages: pdf.numPages, sizes, info, modified: false, history: [] }
      dispatch({ type: 'add', doc: d })
      return d
    } catch (e) { toast('err', `${t('error')}: ${String((e as Error)?.message || e)}`); return null }
    finally { dispatch({ type: 'busy', busy: null }) }
  }, [state.docs, dispatch, toast])

  const openPaths = useCallback(async (paths: string[]) => { for (const p of paths) await openPath(p) }, [openPath])
  const openBlobs = useCallback(async (files: File[]) => {
    for (const f of files) {
      dispatch({ type: 'busy', busy: t('loading') })
      try {
        const bytes = await f.arrayBuffer()
        let password = ''; let pdf = null as Awaited<ReturnType<typeof loadPdf>> | null; let wrong = false
        for (let i = 0; i < 5 && !pdf; i++) { try { pdf = await loadPdf(bytes, password) } catch (e) { if (e instanceof NeedsPassword) { const p = await askPassword(f.name, wrong || e.wrong); if (p === null) break; password = p; wrong = true; continue } throw e } }
        if (!pdf) continue
        const sizes = await pageSizes(pdf)
        dispatch({ type: 'add', doc: { id: 'd' + ++seq, name: f.name, path: f.name, workPath: f.name, password, pdf, pages: pdf.numPages, sizes, info: null, modified: false, history: [] } })
      } catch (e) { toast('err', String((e as Error)?.message || e)) } finally { dispatch({ type: 'busy', busy: null }) }
    }
  }, [dispatch, toast])
  const fileInput = useRef<HTMLInputElement>(null)
  const onOpen = async () => { if (!isTauri()) { fileInput.current?.click(); return } const ps = await pickPdfs(true); await openPaths(ps) }

  // drag & drop + OS open events
  useEffect(() => onDrop((paths) => { void openPaths(paths) }, (over) => dispatch({ type: 'dragging', on: over })), [openPaths, dispatch])
  useEffect(() => onOpenFiles((req) => { void openPaths(req.paths) }), [openPaths])
  useEffect(() => { if (isTauri()) installSelfTestPoller() }, [])
  useEffect(() => { if (isTauri()) takePending().then((reqs) => { for (const r of reqs) void openPaths(r.paths) }).catch(() => {}) }, [])
  useEffect(() => { if (pw) setTimeout(() => pwInput.current?.focus(), 30) }, [pw])

  const saveAs = async () => {
    if (!doc) return
    const dest = await pickSavePath(doc.path)
    if (!dest) return
    try { await copyFile(doc.workPath, dest); dispatch({ type: 'replace', id: doc.id, patch: { path: dest, name: basename(dest), modified: false } }); toast('ok', `${t('saved')}: ${basename(dest)}`) }
    catch (e) { toast('err', String(e)) }
  }
  const closeDoc = (id: string) => { const d = state.docs.find((x) => x.id === id); (d?.pdf as unknown as { destroy?: () => void })?.destroy?.(); dispatch({ type: 'remove', id }) }

  const pageMenu = (_p: number): MenuItem[] => [
    { label: t('rotateL'), icon: 'fa-rotate-left', onClick: () => window.dispatchEvent(new CustomEvent('tool', { detail: 'rotateL' })) },
    { label: t('rotateR'), icon: 'fa-rotate-right', onClick: () => window.dispatchEvent(new CustomEvent('tool', { detail: 'rotate' })) },
    { label: t('extract'), icon: 'fa-file-export', onClick: () => window.dispatchEvent(new CustomEvent('tool', { detail: 'extract' })) },
    { sep: true, label: '' },
    { label: t('deletePages'), icon: 'fa-trash-can', danger: true, onClick: () => window.dispatchEvent(new CustomEvent('tool', { detail: 'delete' })) },
  ]
  const fileMenu = (d: DocState): MenuItem[] => [
    { label: t('copyPath'), icon: 'fa-copy', onClick: () => navigator.clipboard.writeText(d.path) },
    { label: t('revealInFolder'), icon: 'fa-folder-open', onClick: () => import('@tauri-apps/plugin-opener').then((m) => m.revealItemInDir(d.path)) },
    { sep: true, label: '' },
    { label: t('closeFile'), icon: 'fa-xmark', danger: true, onClick: () => closeDoc(d.id) },
  ]

  const mainCls = 'main' + (state.showPanel ? '' : ' no-panel') + (state.showSidebar ? '' : ' no-sidebar')
  return (
    <div className={'app' + (state.dragging ? ' dragging' : '')}
      onDragOver={(e) => { if (!isTauri()) { e.preventDefault(); dispatch({ type: 'dragging', on: true }) } }}
      onDragLeave={() => { if (!isTauri()) dispatch({ type: 'dragging', on: false }) }}
      onDrop={(e) => { if (!isTauri()) { e.preventDefault(); dispatch({ type: 'dragging', on: false }); void openBlobs(Array.from(e.dataTransfer.files).filter((f) => /\.pdf$/i.test(f.name))) } }}>
      <Topbar onOpen={onOpen} onSave={saveAs} onSettings={() => setSettings(true)} rerender={rerender} />
      <div className={mainCls}>
        {state.showSidebar && <Sidebar onCloseDoc={closeDoc} pageMenu={pageMenu} fileMenu={fileMenu} />}
        {doc ? <Viewer key={doc.id + doc.workPath} doc={doc} /> : (
          <div className="viewer"><div className="empty"><div className="drop" onClick={onOpen} role="button">
            <i className="fa-regular fa-file-pdf" /><h2>{t('dropTitle')}</h2><div className="muted">{t('dropHint')}</div>
          </div></div></div>
        )}
        {state.showPanel && <Panel doc={doc} />}
      </div>
      {pw && (
        <Modal title={t('password')} icon="fa-lock" onClose={() => { pw.resolve(null); setPw(null) }}
          footer={<button className="btn primary" onClick={() => { pw.resolve(pwInput.current?.value || ''); setPw(null) }}><i className="fa-solid fa-unlock" /> {t('unlock')}</button>}>
          <div>{t('passwordNeeded')}</div>
          <div className="muted" style={{ fontSize: 12 }}>{basename(pw.path)}</div>
          <div className="field"><input ref={pwInput} type="password" autoFocus onKeyDown={(e) => { if (e.key === 'Enter') { pw.resolve(pwInput.current?.value || ''); setPw(null) } }} /></div>
          {pw.wrong && <span className="badge danger">{t('wrongPassword')}</span>}
        </Modal>
      )}
      {settings && <SettingsModal onClose={() => setSettings(false)} />}
      <ToolModals tool={tool} doc={doc} onClose={() => setTool(null)} />
      <input ref={fileInput} type="file" accept="application/pdf" multiple hidden onChange={(e) => { const fs = Array.from(e.target.files || []); e.target.value = ''; void openBlobs(fs) }} />
      <Toasts />
    </div>
  )
}

export default function App() { return <StoreProvider><Shell /></StoreProvider> }
