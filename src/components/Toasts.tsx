import { useStore } from '../lib/store'
const icons = { ok: 'fa-circle-check', err: 'fa-circle-exclamation', warn: 'fa-triangle-exclamation', info: 'fa-circle-info' }
export function Toasts() {
  const { state } = useStore()
  return <div className="toasts">{state.toasts.map((t) => <div key={t.id} className={`toast ${t.kind}`}><i className={`fa-solid ${icons[t.kind]}`} /><span>{t.text}</span></div>)}</div>
}
