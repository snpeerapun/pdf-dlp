import { useStore } from '../lib/store'
import { getLang, setLang, t } from '../i18n'
export function Topbar({ onOpen, onSave, onSettings, rerender }: { onOpen: () => void; onSave: () => void; onSettings: () => void; rerender: () => void }) {
  const { state } = useStore()
  const doc = state.docs.find((d) => d.id === state.activeId)
  return (
    <div className="topbar">
      <div className="brand"><span className="logo"><i className="fa-solid fa-shield-halved" /></span>{t('app')}</div>
      <button className="btn" onClick={onOpen}><i className="fa-regular fa-folder-open" /> {t('open')}</button>
      <button className="btn primary" onClick={onSave} disabled={!doc}><i className="fa-solid fa-floppy-disk" /> {t('saveAs')}</button>
      {doc?.modified && <span className="badge warn dot">{t('modified')}</span>}
      {doc && doc.history.length > 0 && <button className="btn ghost sm" onClick={() => window.dispatchEvent(new CustomEvent('undo'))} title="⌘Z"><i className="fa-solid fa-rotate-left" /> {t('undo')}</button>}
      {state.busy && <span className="badge info"><i className="fa-solid fa-spinner fa-spin" /> {state.busy}</span>}
      <span className="spacer" />
      <div className="seg" title={t('lang')}>
        <button className={getLang() === 'th' ? 'on' : ''} onClick={() => { setLang('th'); rerender() }}>ไทย</button>
        <button className={getLang() === 'en' ? 'on' : ''} onClick={() => { setLang('en'); rerender() }}>EN</button>
      </div>
      <button className="btn ghost icon" onClick={onSettings} title={t('settings')}><i className="fa-solid fa-gear" /></button>
    </div>
  )
}
