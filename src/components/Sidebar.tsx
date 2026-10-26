import { useEffect, useRef, useState } from 'react'
import { renderPage } from '../lib/pdf'
import { useStore, type DocState } from '../lib/store'
import { t } from '../i18n'
import { ContextMenu, type MenuItem } from './ContextMenu'

function Thumb({ doc, index, current, selected, onClick, onContext }: { doc: DocState; index: number; current: boolean; selected: boolean; onClick: (e: React.MouseEvent) => void; onContext: (e: React.MouseEvent) => void }) {
  const ref = useRef<HTMLDivElement>(null)
  const canvas = useRef<HTMLCanvasElement>(null)
  const [done, setDone] = useState(false)
  useEffect(() => {
    const el = ref.current; if (!el) return
    const io = new IntersectionObserver(async (en) => {
      if (!en[0].isIntersecting || done) return
      io.disconnect()
      const page = await doc.pdf.getPage(index + 1)
      if (canvas.current) { await renderPage(page, canvas.current, 150 / page.getViewport({ scale: 1 }).width, undefined, 2); setDone(true) }
    }, { root: el.closest('.scroll'), rootMargin: '300px' })
    io.observe(el); return () => io.disconnect()
  }, [doc.pdf, index, done])
  return (
    <div ref={ref} className={'thumb' + (current ? ' on' : '') + (selected ? ' sel' : '')} onClick={onClick} onContextMenu={onContext}>
      <canvas ref={canvas} style={done ? undefined : { display: 'none' }} />
      {!done && <div className="ph" />}
      <div className="chk">{selected && <i className="fa-solid fa-check" />}</div>
      <div className="num">{index + 1}</div>
    </div>
  )
}

export function Sidebar({ onCloseDoc, pageMenu, fileMenu }: { onCloseDoc: (id: string) => void; pageMenu: (page: number) => MenuItem[]; fileMenu: (doc: DocState) => MenuItem[] }) {
  const { state, dispatch } = useStore()
  const doc = state.docs.find((d) => d.id === state.activeId) || null
  const [menu, setMenu] = useState<{ x: number; y: number; items: MenuItem[] } | null>(null)
  const lastClick = useRef<number | null>(null)

  const toggleSel = (p: number, e: React.MouseEvent) => {
    const sel = new Set(state.selection)
    if (e.shiftKey && lastClick.current) { const [a, b] = [Math.min(lastClick.current, p), Math.max(lastClick.current, p)]; for (let i = a; i <= b; i++) sel.add(i) }
    else if (e.metaKey || e.ctrlKey) { if (sel.has(p)) sel.delete(p); else sel.add(p) }
    else { window.dispatchEvent(new CustomEvent('goto-page', { detail: p })); if (!(e.target as HTMLElement).closest('.chk')) { lastClick.current = p; return } if (sel.has(p)) sel.delete(p); else sel.add(p) }
    lastClick.current = p
    dispatch({ type: 'select', selection: [...sel].sort((a, b) => a - b) })
  }

  return (
    <aside className="sidebar">
      <div className="tabs">
        <button className={state.sidebarTab === 'files' ? 'on' : ''} onClick={() => dispatch({ type: 'sidebarTab', tab: 'files' })}><i className="fa-regular fa-folder-open" /> {t('files')} ({state.docs.length})</button>
        <button className={state.sidebarTab === 'pages' ? 'on' : ''} onClick={() => dispatch({ type: 'sidebarTab', tab: 'pages' })}><i className="fa-regular fa-clone" /> {t('pages')}{doc ? ` (${doc.pages})` : ''}</button>
      </div>
      {state.sidebarTab === 'files' ? (
        <div className="scroll"><div className="filelist">
          {state.docs.map((d) => (
            <div key={d.id} className={'fileitem' + (d.id === state.activeId ? ' on' : '')} onClick={() => dispatch({ type: 'active', id: d.id })} onContextMenu={(e) => { e.preventDefault(); setMenu({ x: e.clientX, y: e.clientY, items: fileMenu(d) }) }} title={d.path}>
              <i className="fa-regular fa-file-pdf" style={{ color: 'var(--danger)' }} />
              <span className="name">{d.name}{d.modified && <span className="muted"> •</span>}</span>
              <span className="meta">{d.pages}</span>
              <button className="btn ghost icon sm x" onClick={(e) => { e.stopPropagation(); onCloseDoc(d.id) }} title={t('closeFile')}><i className="fa-solid fa-xmark" /></button>
            </div>
          ))}
          {state.docs.length === 0 && <div className="muted" style={{ padding: 10 }}>{t('dropHint')}</div>}
        </div></div>
      ) : (
        <>
          {doc && (
            <div className="row" style={{ padding: '8px 10px', borderBottom: '1px solid var(--line)' }}>
              <span className="muted" style={{ fontSize: 12 }}>{state.selection.length ? `${state.selection.length} ${t('selected')}` : t('selectPages')}</span>
              <span className="spacer" />
              <button className="btn ghost sm" onClick={() => dispatch({ type: 'select', selection: Array.from({ length: doc.pages }, (_, i) => i + 1) })}>{t('selectAll')}</button>
              {state.selection.length > 0 && <button className="btn ghost sm icon" onClick={() => dispatch({ type: 'select', selection: [] })} title={t('clearSel')}><i className="fa-solid fa-xmark" /></button>}
            </div>
          )}
          <div className="scroll"><div className="thumbs">
            {doc && Array.from({ length: doc.pages }, (_, i) => (
              <Thumb key={doc.workPath + ':' + i} doc={doc} index={i} current={state.page === i + 1} selected={state.selection.includes(i + 1)}
                onClick={(e) => toggleSel(i + 1, e)}
                onContext={(e) => { e.preventDefault(); if (!state.selection.includes(i + 1)) dispatch({ type: 'select', selection: [i + 1] }); setMenu({ x: e.clientX, y: e.clientY, items: pageMenu(i + 1) }) }} />
            ))}
          </div></div>
        </>
      )}
      {menu && <ContextMenu x={menu.x} y={menu.y} items={menu.items} onClose={() => setMenu(null)} />}
    </aside>
  )
}
