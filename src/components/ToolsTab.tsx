import type { DocState } from '../lib/store'
import { t } from '../i18n'
export function ToolsTab({ doc }: { doc: DocState | null }) {
  const tools: { key: string; icon: string; label: string }[] = [
    { key: 'merge', icon: 'fa-object-group', label: t('merge') }, { key: 'split', icon: 'fa-scissors', label: t('split') }, { key: 'insert', icon: 'fa-file-circle-plus', label: t('insert') },
    { key: 'rotate', icon: 'fa-rotate-right', label: t('rotateR') }, { key: 'delete', icon: 'fa-trash-can', label: t('deletePages') }, { key: 'extract', icon: 'fa-file-export', label: t('extract') },
    { key: 'encrypt', icon: 'fa-lock', label: t('encrypt') }, { key: 'decrypt', icon: 'fa-lock-open', label: t('decrypt') }, { key: 'redact', icon: 'fa-marker', label: t('redact') },
    { key: 'watermark', icon: 'fa-droplet', label: t('watermark') }, { key: 'pagenum', icon: 'fa-list-ol', label: t('pageNumbers') }, { key: 'crop', icon: 'fa-crop-simple', label: t('crop') },
    { key: 'images', icon: 'fa-images', label: t('exportImages') }, { key: 'img2pdf', icon: 'fa-file-image', label: t('images2pdf') }, { key: 'compress', icon: 'fa-compress', label: t('compress') },
    { key: 'sanitize', icon: 'fa-broom', label: t('sanitize') }, { key: 'props', icon: 'fa-pen-to-square', label: t('properties') }, { key: 'print', icon: 'fa-print', label: t('print') },
  ]
  return (
    <div className="card">
      <h4><i className="fa-solid fa-screwdriver-wrench" /> {t('tools')}</h4>
      <div className="toolgrid">
        {tools.map((x) => <button key={x.key} className="tool" disabled={!doc && !['merge', 'img2pdf'].includes(x.key)} onClick={() => window.dispatchEvent(new CustomEvent('tool', { detail: x.key }))}><i className={`fa-solid ${x.icon}`} />{x.label}</button>)}
      </div>
    </div>
  )
}
