import { useEffect, useRef } from 'react'
export interface MenuItem { label: string; icon?: string; onClick?: () => void; danger?: boolean; sep?: boolean; disabled?: boolean }
export function ContextMenu({ x, y, items, onClose }: { x: number; y: number; items: MenuItem[]; onClose: () => void }) {
  const ref = useRef<HTMLDivElement>(null)
  useEffect(() => {
    const down = (e: MouseEvent) => { if (ref.current && e.target instanceof Node && e.target.isConnected && !ref.current.contains(e.target)) onClose() }
    const key = (e: KeyboardEvent) => { if (e.key === 'Escape') onClose() }
    window.addEventListener('mousedown', down); window.addEventListener('keydown', key); window.addEventListener('blur', onClose)
    return () => { window.removeEventListener('mousedown', down); window.removeEventListener('keydown', key); window.removeEventListener('blur', onClose) }
  }, [onClose])
  const px = Math.min(x, window.innerWidth - 230), py = Math.min(y, window.innerHeight - items.length * 34 - 20)
  return (
    <div className="ctx" ref={ref} style={{ left: px, top: py }}>
      {items.map((it, i) => it.sep ? <hr key={i} /> : (
        <button key={i} className={it.danger ? 'danger' : ''} disabled={it.disabled} onClick={() => { onClose(); it.onClick?.() }}>
          <i className={`fa-solid ${it.icon || 'fa-circle-small'}`} /> {it.label}
        </button>
      ))}
    </div>
  )
}
