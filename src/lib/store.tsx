import React, { createContext, useContext, useReducer, useRef } from 'react'
import type { PDFDocumentProxy } from './pdf'
import type { PageSize } from './pdf'
import type { PdfInfo } from './backend'

export interface DocState {
  id: string; name: string; path: string; workPath: string; password: string
  pdf: PDFDocumentProxy; pages: number; sizes: PageSize[]; info: PdfInfo | null; modified: boolean; history: string[]
}
export type Zoom = number | 'width' | 'page'
export interface Toast { id: number; kind: 'ok' | 'err' | 'warn' | 'info'; text: string }
export interface State {
  docs: DocState[]; activeId: string | null; page: number; zoom: Zoom; selection: number[]
  panel: 'tools' | 'classify' | 'info'; sidebarTab: 'files' | 'pages'; showPanel: boolean; showSidebar: boolean
  busy: string | null; toasts: Toast[]; dragging: boolean; search: string
}
type Action =
  | { type: 'add'; doc: DocState }
  | { type: 'remove'; id: string }
  | { type: 'active'; id: string }
  | { type: 'page'; page: number }
  | { type: 'zoom'; zoom: Zoom }
  | { type: 'replace'; id: string; patch: Partial<DocState> }
  | { type: 'select'; selection: number[] }
  | { type: 'panel'; panel: State['panel'] }
  | { type: 'sidebarTab'; tab: State['sidebarTab'] }
  | { type: 'togglePanel' } | { type: 'toggleSidebar' }
  | { type: 'busy'; busy: string | null }
  | { type: 'toast'; toast: Toast } | { type: 'untoast'; id: number }
  | { type: 'dragging'; on: boolean }
  | { type: 'search'; q: string }

const init: State = { docs: [], activeId: null, page: 1, zoom: 'width', selection: [], panel: 'tools', sidebarTab: 'files', showPanel: true, showSidebar: true, busy: null, toasts: [], dragging: false, search: '' }

function reducer(s: State, a: Action): State {
  switch (a.type) {
    case 'add': return { ...s, docs: [...s.docs, a.doc], activeId: a.doc.id, page: 1, selection: [] }
    case 'remove': {
      const docs = s.docs.filter((d) => d.id !== a.id)
      const activeId = s.activeId === a.id ? (docs[docs.length - 1]?.id ?? null) : s.activeId
      return { ...s, docs, activeId, page: 1, selection: [] }
    }
    case 'active': return s.activeId === a.id ? s : { ...s, activeId: a.id, page: 1, selection: [] }
    case 'page': return { ...s, page: a.page }
    case 'zoom': return { ...s, zoom: a.zoom }
    case 'replace': return { ...s, docs: s.docs.map((d) => (d.id === a.id ? { ...d, ...a.patch } : d)), selection: [] }
    case 'select': return { ...s, selection: a.selection }
    case 'panel': return { ...s, panel: a.panel, showPanel: true }
    case 'sidebarTab': return { ...s, sidebarTab: a.tab, showSidebar: true }
    case 'togglePanel': return { ...s, showPanel: !s.showPanel }
    case 'toggleSidebar': return { ...s, showSidebar: !s.showSidebar }
    case 'busy': return { ...s, busy: a.busy }
    case 'toast': return { ...s, toasts: [...s.toasts, a.toast].slice(-5) }
    case 'untoast': return { ...s, toasts: s.toasts.filter((t) => t.id !== a.id) }
    case 'dragging': return s.dragging === a.on ? s : { ...s, dragging: a.on }
    case 'search': return { ...s, search: a.q }
  }
}

const Ctx = createContext<{ state: State; dispatch: React.Dispatch<Action>; toast: (kind: Toast['kind'], text: string) => void } | null>(null)
export function StoreProvider({ children }: { children: React.ReactNode }) {
  const [state, dispatch] = useReducer(reducer, init)
  const seq = useRef(0)
  const toast = (kind: Toast['kind'], text: string) => {
    const id = ++seq.current
    dispatch({ type: 'toast', toast: { id, kind, text } })
    setTimeout(() => dispatch({ type: 'untoast', id }), kind === 'err' ? 7000 : 3500)
  }
  return <Ctx.Provider value={{ state, dispatch, toast }}>{children}</Ctx.Provider>
}
export const useStore = () => { const c = useContext(Ctx); if (!c) throw new Error('store'); return c }
export const useActiveDoc = () => { const { state } = useStore(); return state.docs.find((d) => d.id === state.activeId) || null }
