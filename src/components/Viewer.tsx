import React, { useCallback, useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react'
import * as pdfjs from 'pdfjs-dist'
import { enqueue, isCancel, renderToCanvas, type PDFPageProxy } from '../lib/pdf'
import { useStore, type DocState, type Zoom } from '../lib/store'
import { t } from '../i18n'

const PAD = 22, GAP = 18
const ZOOM_STEPS = [0.25, 0.33, 0.5, 0.67, 0.75, 0.9, 1, 1.1, 1.25, 1.5, 1.75, 2, 2.5, 3, 4]

function scaleFor(zoom: Zoom, w: number, h: number, boxW: number, boxH: number) {
  if (zoom === 'width') return Math.max(0.1, (boxW - PAD * 2 - 12) / w)
  if (zoom === 'page') return Math.max(0.1, Math.min((boxW - PAD * 2 - 12) / w, (boxH - PAD * 2) / h))
  return zoom
}

/** One page. Keeps the last rendered canvas on screen (CSS-scaled) until a crisp re-render is ready, then swaps. */
const PageView = React.memo(function PageView({ doc, index, scale, visible, currentPage, onReady }: { doc: DocState; index: number; scale: number; visible: boolean; currentPage: number; onReady: (el: HTMLDivElement | null) => void }) {
  const ref = useRef<HTMLDivElement>(null)
  const holder = useRef<HTMLDivElement>(null)
  const textRef = useRef<HTMLDivElement>(null)
  const pageRef = useRef<PDFPageProxy | null>(null)
  const renderedScale = useRef(0)
  const gen = useRef(0)
  const [hasCanvas, setHasCanvas] = useState(false)
  const size = doc.sizes[index]
  const w = Math.floor(size.width * scale), h = Math.floor(size.height * scale)

  useEffect(() => { onReady(ref.current); return () => onReady(null) }, [onReady])

  // Keep whatever is on screen scaled to the new size instantly (no blank page while zooming).
  useLayoutEffect(() => {
    const c = holder.current?.firstElementChild as HTMLCanvasElement | undefined
    if (c) { c.style.width = `${w}px`; c.style.height = `${h}px` }
    if (textRef.current) textRef.current.style.display = renderedScale.current === scale ? '' : 'none'
  }, [w, h, scale])

  useEffect(() => {
    if (!visible || renderedScale.current === scale) return
    const myGen = ++gen.current
    const signal = { cancelled: false }
    const cancelled = () => signal.cancelled || myGen !== gen.current
    // Debounce zoom bursts; closer-to-current pages render first.
    const timer = setTimeout(() => {
      enqueue(Math.abs(index + 1 - currentPage), cancelled, async () => {
        if (cancelled()) return
        try {
          const page = pageRef.current || (pageRef.current = await doc.pdf.getPage(index + 1))
          if (cancelled()) return
          const { canvas, viewport } = await renderToCanvas(page, scale, undefined, undefined, signal)
          if (cancelled() || !holder.current) return
          holder.current.replaceChildren(canvas)
          renderedScale.current = scale
          setHasCanvas(true)
          const tl = textRef.current
          if (tl) {
            tl.style.display = ''
            tl.innerHTML = ''
            tl.style.setProperty('--scale-factor', String(viewport.scale))
            const run = () => { if (!cancelled()) new pdfjs.TextLayer({ textContentSource: page.streamTextContent(), container: tl, viewport }).render().catch(() => {}) }
            if ('requestIdleCallback' in window) (window as Window & { requestIdleCallback: (cb: () => void) => void }).requestIdleCallback(run); else setTimeout(run, 50)
          }
        } catch (e) { if (!isCancel(e)) console.error('render', e) }
      })
    }, renderedScale.current ? 140 : 0)
    return () => { signal.cancelled = true; clearTimeout(timer) }
  }, [visible, scale, doc.pdf, index, currentPage])

  return (
    <div ref={ref} className="page" data-page={index + 1} style={{ width: w, height: h }}>
      <div ref={holder} className="canvas-holder" />
      <div ref={textRef} className="textLayer" />
      {!hasCanvas && <div className="busy">{t('rendering')}</div>}
    </div>
  )
})

export function Viewer({ doc }: { doc: DocState }) {
  const { state, dispatch } = useStore()
  const box = useRef<HTMLDivElement>(null)
  const [boxSize, setBoxSize] = useState({ w: 0, h: 0 })
  const [visible, setVisible] = useState<Set<number>>(() => new Set([0, 1]))
  const pageEls = useRef<Map<number, HTMLDivElement>>(new Map())
  const io = useRef<IntersectionObserver | null>(null)
  const programmatic = useRef(false)

  useLayoutEffect(() => {
    const el = box.current; if (!el) return
    const ro = new ResizeObserver(() => setBoxSize({ w: el.clientWidth, h: el.clientHeight }))
    ro.observe(el); setBoxSize({ w: el.clientWidth, h: el.clientHeight })
    return () => ro.disconnect()
  }, [])

  const maxW = Math.max(...doc.sizes.map((s) => s.width)), maxH = Math.max(...doc.sizes.map((s) => s.height))
  const scale = useMemo(() => (boxSize.w ? scaleFor(state.zoom, maxW, maxH, boxSize.w, boxSize.h) : 1), [state.zoom, maxW, maxH, boxSize])

  useEffect(() => {
    const el = box.current; if (!el) return
    io.current = new IntersectionObserver((entries) => {
      setVisible((prev) => {
        const next = new Set(prev)
        for (const en of entries) { const i = Number((en.target as HTMLElement).dataset.page) - 1; if (en.isIntersecting) next.add(i); else next.delete(i) }
        return next
      })
    }, { root: el, rootMargin: '900px 0px' })
    pageEls.current.forEach((p) => io.current!.observe(p))
    return () => { io.current?.disconnect(); io.current = null }
  }, [doc.id])

  const readyFns = useMemo(() => Array.from({ length: doc.pages }, (_, i) => (el: HTMLDivElement | null) => {
    if (el) { pageEls.current.set(i, el); io.current?.observe(el) } else { const old = pageEls.current.get(i); if (old) io.current?.unobserve(old); pageEls.current.delete(i) }
  }), [doc.pages])

  const onScroll = () => {
    if (programmatic.current) return
    const el = box.current; if (!el) return
    const top = el.scrollTop + el.clientHeight * 0.35
    let best = 1, bestD = Infinity
    pageEls.current.forEach((p, i) => { const d = Math.abs(p.offsetTop - top); if (d < bestD) { bestD = d; best = i + 1 } })
    if (best !== state.page) dispatch({ type: 'page', page: best })
  }

  const goto = useCallback((n: number) => {
    const p = Math.min(Math.max(1, n), doc.pages)
    const el = pageEls.current.get(p - 1); const b = box.current
    if (el && b) { programmatic.current = true; b.scrollTo({ top: el.offsetTop - PAD }); setTimeout(() => { programmatic.current = false }, 80) }
    dispatch({ type: 'page', page: p })
  }, [doc.pages, dispatch])
  useEffect(() => { const h = (e: Event) => goto((e as CustomEvent<number>).detail); window.addEventListener('goto-page', h); return () => window.removeEventListener('goto-page', h) }, [goto])

  const setZoom = useCallback((z: Zoom) => {
    // keep the same document point under the viewport centre
    const b = box.current
    if (b && typeof z === 'number') {
      const ratio = z / scale
      const cx = b.scrollLeft + b.clientWidth / 2, cy = b.scrollTop + b.clientHeight / 2
      requestAnimationFrame(() => { b.scrollTo({ left: cx * ratio - b.clientWidth / 2, top: cy * ratio - b.clientHeight / 2 }) })
    }
    dispatch({ type: 'zoom', zoom: z })
  }, [scale, dispatch])
  const step = useCallback((dir: 1 | -1) => {
    const cur = scale
    const next = dir > 0 ? ZOOM_STEPS.find((s) => s > cur + 0.001) : [...ZOOM_STEPS].reverse().find((s) => s < cur - 0.001)
    if (next) setZoom(next)
  }, [scale, setZoom])

  useEffect(() => {
    const el = box.current; if (!el) return
    let acc = 0, raf = 0
    const wheel = (e: WheelEvent) => {
      if (!(e.ctrlKey || e.metaKey)) return
      e.preventDefault(); acc += e.deltaY
      if (!raf) raf = requestAnimationFrame(() => { raf = 0; if (Math.abs(acc) > 8) { step(acc < 0 ? 1 : -1) } acc = 0 })
    }
    el.addEventListener('wheel', wheel, { passive: false }); return () => el.removeEventListener('wheel', wheel)
  }, [step])
  useEffect(() => {
    const key = (e: KeyboardEvent) => {
      if (!(e.metaKey || e.ctrlKey)) return
      if (e.key === '=' || e.key === '+') { e.preventDefault(); step(1) } else if (e.key === '-') { e.preventDefault(); step(-1) } else if (e.key === '0') { e.preventDefault(); setZoom('width') }
    }
    window.addEventListener('keydown', key); return () => window.removeEventListener('keydown', key)
  }, [step, setZoom])

  return (
    <div className="viewer">
      <div className="viewer-toolbar">
        <button className="btn ghost icon sm" title={t('files')} onClick={() => dispatch({ type: 'toggleSidebar' })}><i className="fa-solid fa-table-columns" /></button>
        <span className="pagebox">
          <button className="btn ghost icon sm" onClick={() => goto(state.page - 1)} disabled={state.page <= 1}><i className="fa-solid fa-chevron-up" /></button>
          <input className="pageinput" value={state.page} onChange={(e) => { const v = Number(e.target.value); if (v) goto(v) }} />
          <span>/ {doc.pages}</span>
          <button className="btn ghost icon sm" onClick={() => goto(state.page + 1)} disabled={state.page >= doc.pages}><i className="fa-solid fa-chevron-down" /></button>
        </span>
        <span className="spacer" />
        <button className="btn ghost icon sm" title={t('zoomOut')} onClick={() => step(-1)}><i className="fa-solid fa-minus" /></button>
        <span className="zoom-val">{Math.round(scale * 100)}%</span>
        <button className="btn ghost icon sm" title={t('zoomIn')} onClick={() => step(1)}><i className="fa-solid fa-plus" /></button>
        <div className="seg">
          <button className={state.zoom === 'width' ? 'on' : ''} onClick={() => setZoom('width')} title={t('fitWidth')}><i className="fa-solid fa-arrows-left-right" /></button>
          <button className={state.zoom === 'page' ? 'on' : ''} onClick={() => setZoom('page')} title={t('fitPage')}><i className="fa-solid fa-expand" /></button>
        </div>
        <span className="spacer" />
        <button className="btn ghost icon sm" title={t('tools')} onClick={() => dispatch({ type: 'togglePanel' })}><i className="fa-solid fa-sliders" /></button>
      </div>
      <div className="pages" ref={box} onScroll={onScroll} style={{ gap: GAP, padding: PAD }}>
        {boxSize.w > 0 && Array.from({ length: doc.pages }, (_, i) => <PageView key={i} doc={doc} index={i} scale={scale} visible={visible.has(i)} currentPage={state.page} onReady={readyFns[i]} />)}
      </div>
    </div>
  )
}
