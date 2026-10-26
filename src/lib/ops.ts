// High-level document operations used by tools + classification. Every mutation goes through Rust and
// produces a new temp file; the viewer reloads it. The original file is untouched until "Save as".
import { useCallback } from 'react'
import { useStore, type DocState } from './store'
import { loadPdf, pageSizes } from './pdf'
import { applyOps, basename, mergePdfs, pdfInfo, readFile, type Op } from './backend'
import { t } from '../i18n'

let seq = 1000
export function useOps() {
  const { state, dispatch, toast } = useStore()
  const active = state.docs.find((d) => d.id === state.activeId) || null

  const loadDoc = useCallback(async (path: string, password: string, name?: string): Promise<Omit<DocState, 'id' | 'history' | 'modified' | 'path'>> => {
    const bytes = await readFile(path)
    const pdf = await loadPdf(bytes, password)
    const sizes = await pageSizes(pdf)
    const info = await pdfInfo(path, password).catch(() => null)
    return { name: name || basename(path), workPath: path, password, pdf, pages: pdf.numPages, sizes, info }
  }, [])

  /** Apply ops to a doc and reload the result into the same tab. */
  const run = useCallback(async (doc: DocState, ops: Op[], opts: { nextPassword?: string; busy?: string; keepPage?: boolean } = {}) => {
    dispatch({ type: 'busy', busy: opts.busy || t('loading') })
    try {
      const res = await applyOps(doc.workPath, doc.password, ops)
      const pw = opts.nextPassword ?? (ops.some((o) => o.op === 'decrypt') ? '' : doc.password)
      const loaded = await loadDoc(res.path, pw, doc.name)
      ;(doc.pdf as unknown as { destroy?: () => void }).destroy?.()
      dispatch({ type: 'replace', id: doc.id, patch: { ...loaded, modified: true, history: [...doc.history, doc.workPath].slice(-20) } })
      return res
    } catch (e) { toast('err', `${t('error')}: ${String(e)}`); throw e }
    finally { dispatch({ type: 'busy', busy: null }) }
  }, [dispatch, loadDoc, toast])

  /** Open a produced file (merge/extract/images→pdf) as a new tab, marked modified (lives in temp until saved). */
  const openProduced = useCallback(async (path: string, name: string, password = '') => {
    dispatch({ type: 'busy', busy: t('loading') })
    try {
      const loaded = await loadDoc(path, password, name)
      const d: DocState = { id: 'p' + ++seq, path: name, ...loaded, modified: true, history: [] }
      dispatch({ type: 'add', doc: d })
      return d
    } catch (e) { toast('err', String(e)); throw e }
    finally { dispatch({ type: 'busy', busy: null }) }
  }, [dispatch, loadDoc, toast])

  const undo = useCallback(async (doc: DocState) => {
    const prev = doc.history[doc.history.length - 1]
    if (!prev) return
    dispatch({ type: 'busy', busy: t('loading') })
    try {
      const loaded = await loadDoc(prev, doc.password, doc.name)
      dispatch({ type: 'replace', id: doc.id, patch: { ...loaded, modified: prev !== doc.path, history: doc.history.slice(0, -1) } })
    } catch (e) { toast('err', String(e)) } finally { dispatch({ type: 'busy', busy: null }) }
  }, [dispatch, loadDoc, toast])

  const pagesOrSel = (doc: DocState) => (state.selection.length ? state.selection : [state.page]).filter((p) => p >= 1 && p <= doc.pages)

  return {
    active, run, openProduced, undo, loadDoc,
    rotate: (doc: DocState, angle: number, pages?: number[]) => run(doc, [{ op: 'rotate', pages: pages ?? pagesOrSel(doc), angle }]),
    deletePages: (doc: DocState, pages?: number[]) => run(doc, [{ op: 'delete', pages: pages ?? pagesOrSel(doc) }]),
    reorder: (doc: DocState, order: number[]) => run(doc, [{ op: 'reorder', order }]),
    extract: async (doc: DocState, pages?: number[]) => {
      const ps = pages ?? pagesOrSel(doc)
      const res = await applyOps(doc.workPath, doc.password, [{ op: 'extract', pages: ps }])
      return openProduced(res.path, doc.name.replace(/\.pdf$/i, '') + `-p${ps[0]}${ps.length > 1 ? '-' + ps[ps.length - 1] : ''}.pdf`, doc.password)
    },
    merge: async (inputs: { path: string; password?: string; pages?: number[] }[], name = 'merged.pdf') => { const res = await mergePdfs(inputs); return openProduced(res.path, name) },
    insertAt: async (doc: DocState, other: { path: string; password?: string }, afterPage: number) => {
      const before = Array.from({ length: afterPage }, (_, i) => i + 1)
      const after = Array.from({ length: doc.pages - afterPage }, (_, i) => afterPage + i + 1)
      const inputs = [] as { path: string; password?: string; pages?: number[] }[]
      if (before.length) inputs.push({ path: doc.workPath, password: doc.password, pages: before })
      inputs.push(other)
      if (after.length) inputs.push({ path: doc.workPath, password: doc.password, pages: after })
      dispatch({ type: 'busy', busy: t('loading') })
      try {
        const res = await mergePdfs(inputs)
        const loaded = await loadDoc(res.path, '', doc.name)
        dispatch({ type: 'replace', id: doc.id, patch: { ...loaded, password: '', modified: true, history: [...doc.history, doc.workPath] } })
      } catch (e) { toast('err', String(e)); throw e } finally { dispatch({ type: 'busy', busy: null }) }
    },
    pagesOrSel,
  }
}
