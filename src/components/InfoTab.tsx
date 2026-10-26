import { useEffect, useState } from 'react'
import { metadata } from '../lib/pdf'
import type { DocState } from '../lib/store'
import { t } from '../i18n'

function fmtDate(v: unknown) {
  if (typeof v !== 'string') return ''
  const m = v.match(/D:(\d{4})(\d{2})(\d{2})(\d{2})?(\d{2})?/)
  return m ? `${m[1]}-${m[2]}-${m[3]} ${m[4] ?? '00'}:${m[5] ?? '00'}` : v
}
export function InfoTab({ doc }: { doc: DocState | null }) {
  const [meta, setMeta] = useState<Record<string, unknown>>({})
  useEffect(() => { if (doc) metadata(doc.pdf).then(setMeta); else setMeta({}) }, [doc?.workPath, doc?.pdf])
  if (!doc) return <div className="muted">{t('dropHint')}</div>
  const info = doc.info
  const cls = info?.classification || info?.msip?.name
  return (
    <>
      <div className="card">
        <h4><i className="fa-solid fa-tag" /> {t('classification')}</h4>
        <div className="row">{cls ? <span className="badge warn dot">{cls}</span> : <span className="badge">{t('noLabel')}</span>}
          {info?.has_marks && <span className="badge info">{t('marks')}: {t('present')}</span>}</div>
        {info?.msip && (
          <dl className="kv">
            <dt>{t('msipLabel')}</dt><dd>{info.msip.name}</dd>
            <dt>GUID</dt><dd className="mono">{info.msip.id}</dd>
            <dt>Set date</dt><dd>{info.msip.set_date}</dd>
            {info.msip.method && <><dt>Method</dt><dd>{info.msip.method}</dd></>}
            {info.msip.site_id && <><dt>Tenant</dt><dd className="mono">{info.msip.site_id}</dd></>}
          </dl>
        )}
      </div>
      <div className="card">
        <h4><i className="fa-regular fa-file-lines" /> {t('properties')}</h4>
        <dl className="kv">
          <dt>{t('title')}</dt><dd>{String(meta.Title || '')}</dd>
          <dt>{t('author')}</dt><dd>{String(meta.Author || '')}</dd>
          <dt>{t('subject')}</dt><dd>{String(meta.Subject || '')}</dd>
          <dt>{t('keywords')}</dt><dd>{String(meta.Keywords || '')}</dd>
          <dt>{t('producer')}</dt><dd>{String(meta.Producer || meta.Creator || '')}</dd>
          <dt>{t('created')}</dt><dd>{fmtDate(meta.CreationDate)}</dd>
          <dt>{t('modifiedAt')}</dt><dd>{fmtDate(meta.ModDate)}</dd>
          <dt>{t('version')}</dt><dd>{String(meta.PDFFormatVersion || info?.version || '')}</dd>
          <dt>{t('pages')}</dt><dd>{doc.pages}</dd>
          <dt>{t('encrypted')}</dt><dd>{info?.encrypted ? <span className="badge danger dot">{t('yes')}</span> : <span className="badge ok dot">{t('no')}</span>}</dd>
          <dt>{t('path')}</dt><dd className="select-text" style={{ fontSize: 12 }}>{doc.path}</dd>
        </dl>
      </div>
      {info && Object.keys(info.metadata).some((k) => k.startsWith('MSIP_') || k === 'Classification') && (
        <div className="card"><h4><i className="fa-solid fa-code" /> Metadata</h4>
          <dl className="kv">{Object.entries(info.metadata).filter(([k]) => k.startsWith('MSIP_') || k === 'Classification').map(([k, v]) => <><dt key={k + 'k'} className="mono" style={{ fontSize: 10.5 }}>{k.replace(/^MSIP_Label_[0-9a-f-]+_/, 'MSIP…_')}</dt><dd key={k} className="mono">{v}</dd></>)}</dl>
        </div>
      )}
    </>
  )
}
