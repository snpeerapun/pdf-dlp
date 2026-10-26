// Browser harness: open fixture in the web build (no Tauri), screenshot + zoom timing.
// Run from a folder with playwright installed:  node ~/Projects/pdf-dlp/tools/ui-test.mjs [url] [pdf]
import { chromium } from 'playwright'
const url = process.argv[2] || 'http://localhost:1420/'
const pdf = process.argv[3] || '/Users/peerapuns/Projects/pdf-dlp/fixtures/hr-confidential.pdf'
const out = '/tmp/pdfdlp-ui'
import fs from 'node:fs'; fs.mkdirSync(out, { recursive: true })
const browser = await chromium.launch({ channel: 'chrome', headless: true, args: ['--enable-unsafe-swiftshader'] })
const page = await browser.newPage({ viewport: { width: 1380, height: 860 }, deviceScaleFactor: 1 })
const errors = []
page.on('pageerror', (e) => errors.push('pageerror: ' + e.message))
page.on('console', (m) => { if (m.type() === 'error') errors.push('console: ' + m.text().slice(0, 200)) })
await page.goto(url, { waitUntil: 'domcontentloaded' })
await page.setInputFiles('input[type=file]', pdf)
await page.waitForSelector('.page canvas', { timeout: 20000 })
await page.waitForTimeout(1200)
await page.screenshot({ path: `${out}/01-open.png` })
const t0 = Date.now()
for (let i = 0; i < 4; i++) { await page.click('button[title="ซูมเข้า"]'); await page.waitForTimeout(60) }
await page.screenshot({ path: `${out}/02-zoom-burst.png` })
let tZoom = -1
try {
  await page.waitForFunction(() => { const q = window.__pdfq?.(); return q && q.running === 0 && q.queued === 0 && q.done >= 4 }, null, { timeout: 15000 })
  tZoom = Date.now() - t0
} catch { console.log('zoom wait timeout', await page.evaluate(() => window.__pdfq?.())) }
await page.waitForTimeout(800)
await page.screenshot({ path: `${out}/03-zoomed.png` })
const info = await page.evaluate(() => ({ q: window.__pdfq?.(), pageBox: (() => { const p = document.querySelector('.page'); return p ? [p.getBoundingClientRect().width, p.getBoundingClientRect().height] : null })(), zoom: document.querySelector('.zoom-val')?.textContent, pages: document.querySelectorAll('.page').length, canvases: document.querySelectorAll('.page canvas').length, firstCanvas: (() => { const c = document.querySelector('.page canvas'); return c ? [c.width, c.height, c.style.width] : null })() }))
await page.click('.tabs button:nth-child(2)')
await page.waitForTimeout(1500)
await page.screenshot({ path: `${out}/04-thumbs.png` })
console.log(JSON.stringify({ tZoomMs: tZoom, info, errors: errors.slice(0, 8) }, null, 1))
await browser.close()
