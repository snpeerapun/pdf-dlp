import { useEffect, useState } from 'react'
import { Modal } from './Modal'
import { t } from '../i18n'
import { shellRegister, shellStatus, shellUnregister } from '../lib/backend'
import { useStore } from '../lib/store'

export function SettingsModal({ onClose }: { onClose: () => void }) {
  const { toast } = useStore()
  const [st, setSt] = useState<{ supported: boolean; registered: boolean } | null>(null)
  const refresh = () => shellStatus().then(setSt).catch(() => setSt({ supported: false, registered: false }))
  useEffect(() => { refresh() }, [])
  const reg = async () => {
    try { await shellRegister([{ action: 'open', label: t('menuOpen') }, { action: 'classify', label: t('menuClassify') }, { action: 'encrypt', label: t('menuEncrypt') }]); toast('ok', t('registered')); refresh() }
    catch (e) { toast('err', String(e)) }
  }
  const unreg = async () => { try { await shellUnregister(); refresh() } catch (e) { toast('err', String(e)) } }
  return (
    <Modal title={t('settings')} icon="fa-gear" onClose={onClose}>
      <div className="card">
        <h4><i className="fa-brands fa-windows" /> {t('contextMenu')}</h4>
        <div className="hint">{t('menuOpen')} · {t('menuClassify')} · {t('menuEncrypt')}</div>
        <div className="row">
          {st && (st.supported ? <span className={'badge dot ' + (st.registered ? 'ok' : '')}>{st.registered ? t('registered') : t('notRegistered')}</span> : <span className="badge">{t('winOnly')}</span>)}
          <span className="spacer" />
          <button className="btn" disabled={!st?.supported} onClick={reg}><i className="fa-solid fa-plus" /> {t('register')}</button>
          <button className="btn ghost" disabled={!st?.supported || !st.registered} onClick={unreg}>{t('unregister')}</button>
        </div>
      </div>
    </Modal>
  )
}
