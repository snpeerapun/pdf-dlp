import { useStore, type DocState } from '../lib/store'
import { t } from '../i18n'
import { ToolsTab } from './ToolsTab'
import { ClassifyTab } from './ClassifyTab'
import { InfoTab } from './InfoTab'

export function Panel({ doc }: { doc: DocState | null }) {
  const { state, dispatch } = useStore()
  return (
    <aside className="panel">
      <div className="tabs">
        <button className={state.panel === 'tools' ? 'on' : ''} onClick={() => dispatch({ type: 'panel', panel: 'tools' })}><i className="fa-solid fa-screwdriver-wrench" /> {t('tools')}</button>
        <button className={state.panel === 'classify' ? 'on' : ''} onClick={() => dispatch({ type: 'panel', panel: 'classify' })}><i className="fa-solid fa-tag" /> {t('classify')}</button>
        <button className={state.panel === 'info' ? 'on' : ''} onClick={() => dispatch({ type: 'panel', panel: 'info' })}><i className="fa-solid fa-circle-info" /> {t('info')}</button>
      </div>
      <div className="body">
        {state.panel === 'tools' && <ToolsTab doc={doc} />}
        {state.panel === 'classify' && <ClassifyTab doc={doc} />}
        {state.panel === 'info' && <InfoTab doc={doc} />}
      </div>
    </aside>
  )
}
