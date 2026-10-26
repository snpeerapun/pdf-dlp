// Classification labels (MSIP-compatible) kept in localStorage; discovery via Rust.
import type { Label } from './backend'
const KEY = 'pdfdlp.labels.v1'
export interface LabelPrefs { siteId: string; owner: string; writeMsip: boolean; header: boolean; footer: boolean; watermark: boolean; encrypt: boolean }
const PKEY = 'pdfdlp.labelprefs.v1'
export const DEFAULT_COLORS = ['#2ea043', '#4f8cff', '#f4b63f', '#ef5b5b', '#9b5cff']
export function loadLabels(): Label[] { try { return JSON.parse(localStorage.getItem(KEY) || '[]') } catch { return [] } }
export function saveLabels(l: Label[]) { localStorage.setItem(KEY, JSON.stringify(l)) }
export function mergeLabels(cur: Label[], add: Label[]): Label[] {
  const out = [...cur]
  for (const a of add) { const i = out.findIndex((x) => x.id === a.id); if (i >= 0) out[i] = { ...out[i], name: a.name || out[i].name, tooltip: a.tooltip || out[i].tooltip, color: out[i].color || a.color, source: out[i].source || a.source }; else out.push({ ...a, color: a.color || DEFAULT_COLORS[out.length % DEFAULT_COLORS.length] }) }
  return out.map((l, i) => ({ ...l, order: i }))
}
export function loadPrefs(): LabelPrefs { try { return { siteId: '', owner: '', writeMsip: true, header: true, footer: true, watermark: false, encrypt: false, ...JSON.parse(localStorage.getItem(PKEY) || '{}') } } catch { return { siteId: '', owner: '', writeMsip: true, header: true, footer: true, watermark: false, encrypt: false } } }
export function savePrefs(p: LabelPrefs) { localStorage.setItem(PKEY, JSON.stringify(p)) }
/** MSIP ContentBits: 1 header, 2 footer, 4 watermark, 8 encrypt */
export const contentBits = (p: LabelPrefs) => (p.header ? 1 : 0) | (p.footer ? 2 : 0) | (p.watermark ? 4 : 0) | (p.encrypt ? 8 : 0)
