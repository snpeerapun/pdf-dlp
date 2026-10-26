// Bridge to the Rust core. All PDF mutations happen in Rust; the UI only renders.
import { invoke } from '@tauri-apps/api/core'
import { open as openDialog, save as saveDialog, ask } from '@tauri-apps/plugin-dialog'
import { listen } from '@tauri-apps/api/event'
import { getCurrentWebview } from '@tauri-apps/api/webview'

export const isTauri = () => typeof window !== 'undefined' && '__TAURI_INTERNALS__' in window

export type Op =
  | { op: 'rotate'; pages: number[]; angle: number }
  | { op: 'delete'; pages: number[] }
  | { op: 'reorder'; order: number[] }
  | { op: 'extract'; pages: number[] }
  | { op: 'encrypt'; user_password: string; owner_password: string; allow_print: boolean; allow_copy: boolean; allow_modify: boolean; allow_annotate: boolean }
  | { op: 'decrypt' }
  | { op: 'stamp'; pages: number[]; png_base64: string; x: number; y: number; w: number; opacity: number; rotate_deg: number; anchor: 'topleft' | 'center' }
  | { op: 'redact'; page: number; jpeg_base64: string; width_pt: number; height_pt: number }
  | { op: 'metadata'; title?: string | null; subject?: string | null; keywords?: string | null; classification?: string | null; extra?: Record<string, string> }
  | { op: 'remove_marks' }
  | { op: 'crop'; pages: number[]; left: number; top: number; right: number; bottom: number }
  | { op: 'sanitize'; keep_annotations: boolean }

export interface FileLabel { id: string; name: string; enabled: boolean; set_date: string; method: string; site_id: string; content_bits: string; action_id: string; owner: string }
export interface PdfInfo { pages: number; encrypted: boolean; needs_password: boolean; version: string; title: string | null; classification: string | null; has_marks: boolean; metadata: Record<string, string>; msip: FileLabel | null }
export interface Label { id: string; name: string; tooltip: string; color: string; parent: string; order: number; source: string }
export interface Discovery { labels: Label[]; scanned: string[]; notes: string[] }
export interface OpenRequest { action: string; paths: string[] }

export const readFile = (path: string) => invoke<ArrayBuffer>('read_file', { path })
export const writeFileB64 = (path: string, dataBase64: string) => invoke<void>('write_file_b64', { path, dataBase64 })
export const copyFile = (src: string, dest: string) => invoke<void>('copy_file', { src, dest })
export const pdfInfo = (path: string, password = '') => invoke<PdfInfo>('pdf_info', { path, password })
export const applyOps = (path: string, password: string, ops: Op[], out?: string) => invoke<{ path: string; pages: number }>('apply_ops', { path, password, ops, out: out ?? null })
export const mergePdfs = (inputs: { path: string; password?: string; pages?: number[] }[], out?: string) => invoke<{ path: string; pages: number }>('merge_pdfs', { inputs, out: out ?? null })
export const splitPdf = (path: string, password: string, mode: 'ranges' | 'each' | 'every', spec: string | null, every: number | null, outDir: string) => invoke<{ files: string[] }>('split_pdf', { path, password, mode, spec, every, outDir })
export const imagesToPdf = (paths: string[], fitA4: boolean) => invoke<{ path: string; pages: number }>('images_to_pdf', { paths, fitA4, out: null })
export const fileSize = (path: string) => invoke<number>('file_size', { path })
export const parsePageRanges = (spec: string, total: number) => invoke<number[]>('parse_page_ranges', { spec, total })
export const discoverLabels = () => invoke<Discovery>('discover_labels')
export const labelsFromFile = (path: string, password = '') => invoke<Label[]>('labels_from_file', { path, password })
export const msipMetadata = (label: Label, siteId: string, contentBits: number, owner: string, previous: Record<string, string>) => invoke<Record<string, string>>('msip_metadata', { label, siteId, contentBits, owner, previous })
export const newGuid = () => invoke<string>('new_guid')
export const shellRegister = (items: { action: string; label: string }[]) => invoke<void>('shell_register', { items })
export const shellUnregister = () => invoke<void>('shell_unregister')
export const shellStatus = () => invoke<{ supported: boolean; registered: boolean }>('shell_status')
export const takePending = () => invoke<OpenRequest[]>('take_pending')
export const homeDir = () => invoke<string>('home_dir')

export async function pickPdfs(multiple = true): Promise<string[]> {
  const r = await openDialog({ multiple, directory: false, filters: [{ name: 'PDF', extensions: ['pdf'] }] })
  if (!r) return []
  return Array.isArray(r) ? r : [r]
}
export async function pickImages(): Promise<string[]> {
  const r = await openDialog({ multiple: true, filters: [{ name: 'Images', extensions: ['png', 'jpg', 'jpeg', 'webp'] }] })
  if (!r) return []
  return Array.isArray(r) ? r : [r]
}
export async function pickFolder(): Promise<string | null> {
  const r = await openDialog({ directory: true, multiple: false })
  return typeof r === 'string' ? r : null
}
export async function pickSavePath(defaultName: string, ext = 'pdf'): Promise<string | null> {
  return saveDialog({ defaultPath: defaultName, filters: [{ name: ext.toUpperCase(), extensions: [ext] }] })
}
export const confirm = (msg: string, title = 'PDF DLP') => ask(msg, { title, kind: 'warning' })

export function onDrop(handler: (paths: string[]) => void, onHover?: (over: boolean) => void) {
  if (!isTauri()) return () => {}
  let un: (() => void) | null = null
  getCurrentWebview().onDragDropEvent((ev) => {
    const p = ev.payload
    if (p.type === 'enter' || p.type === 'over') onHover?.(true)
    else if (p.type === 'leave') onHover?.(false)
    else if (p.type === 'drop') { onHover?.(false); handler(p.paths.filter((x) => /\.pdf$/i.test(x))) }
  }).then((u) => { un = u })
  return () => un?.()
}
export function onOpenFiles(handler: (req: OpenRequest) => void) {
  if (!isTauri()) return () => {}
  let un: (() => void) | null = null
  listen<OpenRequest>('open-files', (e) => handler(e.payload)).then((u) => { un = u })
  return () => un?.()
}
export const basename = (p: string) => p.split(/[\\/]/).pop() || p
export const dirname = (p: string) => p.replace(/[\\/][^\\/]*$/, '')
export const stem = (p: string) => basename(p).replace(/\.pdf$/i, '')
