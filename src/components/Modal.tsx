import React, { useEffect } from 'react'
export function Modal({ title, icon, onClose, children, footer, wide }: { title: string; icon?: string; onClose: () => void; children: React.ReactNode; footer?: React.ReactNode; wide?: boolean }) {
  useEffect(() => { const h = (e: KeyboardEvent) => { if (e.key === 'Escape') onClose() }; window.addEventListener('keydown', h); return () => window.removeEventListener('keydown', h) }, [onClose])
  return (
    <div className="modal-bg" onMouseDown={(e) => { if (e.target === e.currentTarget) onClose() }}>
      <div className={'modal' + (wide ? ' wide' : '')} role="dialog" aria-modal="true">
        <header>{icon && <i className={`fa-solid ${icon}`} style={{ color: 'var(--primary)' }} />}<span>{title}</span><button className="btn ghost icon x" onClick={onClose} aria-label="close"><i className="fa-solid fa-xmark" /></button></header>
        <div className="content">{children}</div>
        {footer && <footer>{footer}</footer>}
      </div>
    </div>
  )
}
