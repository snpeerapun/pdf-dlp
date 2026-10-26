// pdf.js wrapper: load (with password), render pages through a priority queue, text content.
import * as pdfjs from 'pdfjs-dist'
import type { PDFDocumentProxy, PDFPageProxy, RenderTask } from 'pdfjs-dist'
import workerUrl from 'pdfjs-dist/build/pdf.worker.min.mjs?url'

pdfjs.GlobalWorkerOptions.workerSrc = workerUrl
export type { PDFDocumentProxy, PDFPageProxy }

export class NeedsPassword extends Error { wrong: boolean; constructor(wrong: boolean) { super(wrong ? 'wrong_password' : 'needs_password'); this.wrong = wrong } }

export async function loadPdf(data: ArrayBuffer, password?: string): Promise<PDFDocumentProxy> {
  try {
    const task = pdfjs.getDocument({ data: new Uint8Array(data.slice(0)), password: password || undefined, useSystemFonts: true })
    return await task.promise
  } catch (e: unknown) {
    const err = e as { name?: string; code?: number }
    if (err?.name === 'PasswordException') throw new NeedsPassword(err.code === 2)
    throw e
  }
}

export interface PageSize { width: number; height: number; rotation: number }
export async function pageSizes(doc: PDFDocumentProxy): Promise<PageSize[]> {
  const out: PageSize[] = []
  for (let i = 1; i <= doc.numPages; i++) {
    const p = await doc.getPage(i)
    const v = p.getViewport({ scale: 1 })
    out.push({ width: v.width, height: v.height, rotation: p.rotate })
  }
  return out
}

export const isCancel = (e: unknown) => (e as { name?: string })?.name === 'RenderingCancelledException'
export const MAX_DPR = 2
export const dpr = () => Math.min(MAX_DPR, window.devicePixelRatio || 1)

/* ---------- render queue: a few concurrent renders, lowest priority value first ---------- */
interface Job { prio: number; run: () => Promise<void>; cancelled: () => boolean }
const queue: Job[] = []
let running = 0
const CONCURRENCY = 2
function pump() {
  while (running < CONCURRENCY && queue.length) {
    queue.sort((a, b) => a.prio - b.prio)
    const job = queue.shift()!
    if (job.cancelled()) continue
    running++
    job.run().catch((e) => console.error('queue job', e)).finally(() => { running--; done++; pump() })
  }
}
export function enqueue(prio: number, cancelled: () => boolean, run: () => Promise<void>) {
  queue.push({ prio, run, cancelled }); pump()
}
export const renderStats = { count: 0, totalMs: 0, last: null as null | { page: number; scale: number; px: number[]; ms: number } }
export const queueStats = () => ({ running, queued: queue.length, done, render: renderStats })
let done = 0
;(window as unknown as { __pdfq: () => unknown }).__pdfq = queueStats

/** Render a page into a *new* canvas (so the old one stays visible until swapped). */
export async function renderToCanvas(page: PDFPageProxy, scale: number, rotation?: number, ratio = dpr(), signal?: { cancelled: boolean }) {
  const viewport = page.getViewport({ scale, rotation: rotation ?? page.rotate })
  const canvas = document.createElement('canvas')
  canvas.width = Math.floor(viewport.width * ratio)
  canvas.height = Math.floor(viewport.height * ratio)
  canvas.style.width = `${Math.floor(viewport.width)}px`
  canvas.style.height = `${Math.floor(viewport.height)}px`
  const ctx = canvas.getContext('2d', { alpha: false })!
  const t0 = performance.now()
  const task: RenderTask = page.render({ canvas, canvasContext: ctx, viewport, transform: ratio !== 1 ? [ratio, 0, 0, ratio, 0, 0] : undefined })
  if (signal) { const t = setInterval(() => { if (signal.cancelled) { task.cancel(); clearInterval(t) } }, 60); try { await task.promise } finally { clearInterval(t) } }
  else await task.promise
  const ms = performance.now() - t0
  renderStats.count++; renderStats.totalMs += ms; renderStats.last = { page: page.pageNumber, scale, px: [canvas.width, canvas.height], ms: Math.round(ms) }
  console.log(`[render] page ${page.pageNumber} scale ${scale.toFixed(2)} ${canvas.width}x${canvas.height} px: ${Math.round(ms)} ms`)
  return { canvas, viewport }
}

/** Convenience for thumbnails: render into an existing canvas (single-shot, guarded). */
const inflight = new WeakMap<HTMLCanvasElement, RenderTask>()
export async function renderPage(page: PDFPageProxy, canvas: HTMLCanvasElement, scale: number, rotation?: number, ratio = dpr()) {
  const prev = inflight.get(canvas)
  if (prev) { prev.cancel(); try { await prev.promise } catch { /* cancelled */ } }
  const viewport = page.getViewport({ scale, rotation: rotation ?? page.rotate })
  canvas.width = Math.floor(viewport.width * ratio); canvas.height = Math.floor(viewport.height * ratio)
  canvas.style.width = `${Math.floor(viewport.width)}px`; canvas.style.height = `${Math.floor(viewport.height)}px`
  const ctx = canvas.getContext('2d', { alpha: false })!
  const task = page.render({ canvas, canvasContext: ctx, viewport, transform: ratio !== 1 ? [ratio, 0, 0, ratio, 0, 0] : undefined })
  inflight.set(canvas, task)
  try { await task.promise } finally { if (inflight.get(canvas) === task) inflight.delete(canvas) }
  return viewport
}

/** Render a page to a JPEG/PNG data URL at a target pixel width (used for redaction / export). */
export async function renderToDataUrl(page: PDFPageProxy, targetWidthPx: number, type: 'image/jpeg' | 'image/png' = 'image/jpeg', quality = 0.9, draw?: (ctx: CanvasRenderingContext2D, w: number, h: number) => void) {
  const base = page.getViewport({ scale: 1 })
  const scale = targetWidthPx / base.width
  const viewport = page.getViewport({ scale })
  const canvas = document.createElement('canvas')
  canvas.width = Math.round(viewport.width); canvas.height = Math.round(viewport.height)
  const ctx = canvas.getContext('2d', { alpha: false })!
  ctx.fillStyle = '#fff'; ctx.fillRect(0, 0, canvas.width, canvas.height)
  await page.render({ canvas, canvasContext: ctx, viewport }).promise
  draw?.(ctx, canvas.width, canvas.height)
  return { dataUrl: canvas.toDataURL(type, quality), width: canvas.width, height: canvas.height, widthPt: base.width, heightPt: base.height }
}

export interface TextItem { str: string; x: number; y: number; w: number; h: number; hasEOL: boolean }
/** Text items in viewer coordinates (scale 1, rotation applied). */
export async function pageText(page: PDFPageProxy): Promise<TextItem[]> {
  const viewport = page.getViewport({ scale: 1 })
  const tc = await page.getTextContent()
  const out: TextItem[] = []
  for (const it of tc.items as Array<{ str: string; transform: number[]; width: number; height: number; hasEOL: boolean }>) {
    if (!('str' in it)) continue
    const tx = pdfjs.Util.transform(viewport.transform, it.transform)
    const fontH = Math.hypot(tx[2], tx[3])
    out.push({ str: it.str, x: tx[4], y: tx[5] - fontH, w: it.width * viewport.scale, h: fontH || it.height, hasEOL: it.hasEOL })
  }
  return out
}

export async function outline(doc: PDFDocumentProxy) { try { return (await doc.getOutline()) || [] } catch { return [] } }
export async function attachments(doc: PDFDocumentProxy): Promise<{ key: string; filename: string; content: Uint8Array }[]> {
  try {
    const a = (await doc.getAttachments()) as unknown
    if (!a) return []
    const out: { key: string; filename: string; content: Uint8Array }[] = []
    const push = (k: string, v: { filename: string; content: Uint8Array }) => out.push({ key: k, filename: v.filename, content: v.content })
    if (a instanceof Map) a.forEach((v, k) => push(k, v as { filename: string; content: Uint8Array }))
    else for (const [k, v] of Object.entries(a as Record<string, { filename: string; content: Uint8Array }>)) push(k, v)
    return out
  } catch { return [] }
}
export async function metadata(doc: PDFDocumentProxy) { try { const m = await doc.getMetadata(); return (m.info as Record<string, unknown>) || {} } catch { return {} } }
