// Canvas-based PNG generators for visual marks (Thai-capable via local Noto Sans Thai).
const FONT = "'Noto Sans Thai', -apple-system, 'Segoe UI', Arial, sans-serif"
export async function ensureFonts() { try { await document.fonts.load(`700 40px ${FONT}`); await document.fonts.load(`500 40px ${FONT}`) } catch { /* ignore */ } }

/** Rounded badge with text, e.g. "CONFIDENTIAL". Returns data URL + aspect (h/w). */
export function badgePng(text: string, bg: string, fg = '#ffffff', fontPx = 44, pad = 18): { dataUrl: string; aspect: number } {
  const c = document.createElement('canvas'); const ctx = c.getContext('2d')!
  ctx.font = `700 ${fontPx}px ${FONT}`
  const tw = Math.ceil(ctx.measureText(text).width)
  c.width = tw + pad * 2; c.height = Math.ceil(fontPx * 1.5) + pad
  ctx.font = `700 ${fontPx}px ${FONT}`; ctx.textBaseline = 'middle'
  const r = 14
  ctx.fillStyle = bg; ctx.beginPath(); ctx.roundRect(0, 0, c.width, c.height, r); ctx.fill()
  ctx.fillStyle = fg; ctx.fillText(text, pad, c.height / 2 + fontPx * 0.06)
  return { dataUrl: c.toDataURL('image/png'), aspect: c.height / c.width }
}

/** Plain text (transparent background) for headers/footers/page numbers/watermarks. */
export function textPng(text: string, color: string, fontPx = 40, weight = 500, opacity = 1): { dataUrl: string; aspect: number } {
  const c = document.createElement('canvas'); const ctx = c.getContext('2d')!
  ctx.font = `${weight} ${fontPx}px ${FONT}`
  const tw = Math.ceil(ctx.measureText(text).width)
  c.width = Math.max(1, tw + 8); c.height = Math.ceil(fontPx * 1.45)
  ctx.font = `${weight} ${fontPx}px ${FONT}`; ctx.textBaseline = 'middle'; ctx.globalAlpha = opacity
  ctx.fillStyle = color; ctx.fillText(text, 4, c.height / 2 + fontPx * 0.05)
  return { dataUrl: c.toDataURL('image/png'), aspect: c.height / c.width }
}

/** Big diagonal watermark text with outline (readable on any background). */
export function watermarkPng(text: string, color: string, fontPx = 120): { dataUrl: string; aspect: number } {
  const c = document.createElement('canvas'); const ctx = c.getContext('2d')!
  ctx.font = `700 ${fontPx}px ${FONT}`
  const tw = Math.ceil(ctx.measureText(text).width)
  c.width = tw + 40; c.height = Math.ceil(fontPx * 1.5)
  ctx.font = `700 ${fontPx}px ${FONT}`; ctx.textBaseline = 'middle'; ctx.textAlign = 'center'
  ctx.lineWidth = fontPx * 0.06; ctx.strokeStyle = 'rgba(255,255,255,0.9)'; ctx.strokeText(text, c.width / 2, c.height / 2 + fontPx * 0.05)
  ctx.fillStyle = color; ctx.fillText(text, c.width / 2, c.height / 2 + fontPx * 0.05)
  return { dataUrl: c.toDataURL('image/png'), aspect: c.height / c.width }
}

export async function imageFileToPng(path: string, readFile: (p: string) => Promise<ArrayBuffer>): Promise<{ dataUrl: string; aspect: number }> {
  const buf = await readFile(path)
  const blob = new Blob([buf]); const url = URL.createObjectURL(blob)
  try {
    const img = await new Promise<HTMLImageElement>((res, rej) => { const i = new Image(); i.onload = () => res(i); i.onerror = rej; i.src = url })
    const c = document.createElement('canvas'); c.width = img.naturalWidth; c.height = img.naturalHeight
    c.getContext('2d')!.drawImage(img, 0, 0)
    return { dataUrl: c.toDataURL('image/png'), aspect: c.height / c.width }
  } finally { URL.revokeObjectURL(url) }
}
