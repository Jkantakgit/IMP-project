import './App.css'

function fetchWithTimeout(input: RequestInfo | URL, init?: RequestInit, timeout = 8000) {
  const controller = new AbortController()
  const id = setTimeout(() => controller.abort(), timeout)
  const finalInit: RequestInit = { ...(init || {}), signal: controller.signal }
  return fetch(input, finalInit).finally(() => clearTimeout(id))
}

type MediaItem = { id: string; name: string }

class App {
  private photos: MediaItem[] = []
  private currentTab: 'photos' | 'live' = 'live'
  private loading = false
  private error: string | null = null
  private busy = false
  private statusMessage: string | null = null

  constructor() {
    this.init()
  }

  private async init() {
    document.getElementById('root')!.innerHTML = this.render()
    this.attachEventListeners()
    await this.loadData()
  }

  private attachEventListeners() {
    // Single-tab UI; no tab switching required
    // Tab switching handlers
    document.getElementById('tab-live')?.addEventListener('click', () => {
      if (this.currentTab === 'live') return
      this.currentTab = 'live'
      this.update()
    })
    document.getElementById('tab-photos')?.addEventListener('click', () => {
      if (this.currentTab === 'photos') return
      // stop MJPEG stream before switching away so the browser/socket is freed
      this.stopMjpeg()
      this.currentTab = 'photos'
      this.update()
    })
    document.getElementById('btn-take')?.addEventListener('click', () => { if (!this.busy) void this.takeMedia() })
    document.getElementById('btn-refresh-photos')?.addEventListener('click', () => { void this.loadData() })
  }


  private async loadData() {
    await this.loadPhotos()
  }

  private async loadPhotos() {
    this.loading = true
    this.error = null
    this.statusMessage = null
    this.update()

    try {
      console.log('[UI] Fetching /photos …')
      const res = await fetchWithTimeout('/photos', { method: 'GET', headers: { Accept: 'application/json' } }, 10000)
      if (!res.ok) throw new Error(`Failed (${res.status})`)
      const text = await res.text()
      console.log('[UI] /photos raw response:', text)
      if (!text) {
        this.photos = []
        return
      }
      const data = JSON.parse(text)
      console.log('[UI] /photos parsed JSON:', data)
      const list = Array.isArray((data as any).files)
        ? (data as any).files
        : Array.isArray(data)
        ? data
        : []
      console.log('[UI] /photos parsed list', list)
      this.photos = list.map((item: any, i: number) =>
        typeof item === 'string'
          ? { id: item, name: item }
          : { id: String(item.name ?? item.id ?? i), name: String(item.name ?? item.id ?? `Photo ${i + 1}`) }
      )
      // Deduplicate entries (some FAT implementations may list files twice)
      this.photos = this.photos.filter((p, idx, arr) => arr.findIndex(x => x.id === p.id) === idx)
    } catch (err) {
      console.error('[UI] /photos error:', err)
      this.error = err instanceof Error ? err.message : 'Unknown error'
      this.photos = []
    } finally {
      this.loading = false
      this.update()
    }
  }

  private async takeMedia() {
    const endpoint = '/photo'
    try {
      console.log('[UI] Trigger', endpoint)
      this.busy = true
      this.statusMessage = 'Fetching device time…'
      this.update()

      // First query device time so we can include a capture timestamp
      const timeRes = await fetchWithTimeout('/time', { method: 'GET', headers: { Accept: 'application/json' } }, 5000)
      if (!timeRes.ok) throw new Error(`Failed to get time (${timeRes.status})`)
      const timeJson = await timeRes.json().catch(() => null)
      const deviceTime = timeJson?.time_ms ?? Date.now()

      this.statusMessage = 'Preparing capture request…'
      this.update()

      // Send plaintext capture command containing the capture timestamp
      const postBody = `capture:${deviceTime}`

      const contentType = postBody.startsWith('{') ? 'application/json' : 'text/plain'
      const res = await fetchWithTimeout(endpoint, { method: 'POST', headers: { 'Content-Type': contentType }, body: postBody }, 15000)
      const text = await res.text()
      let data: any = null
      try { data = text ? JSON.parse(text) : null } catch (e) { data = null }

      if (!res.ok) {
        // server may return 403 with JSON reason
        this.error = data?.reason || `Failed (${res.status})`
        this.statusMessage = null
        this.busy = false
        this.update()
        return
      }

      // Handle response statuses: accepted/scheduled/capturing
      const status = data?.status || (res.status === 202 ? 'accepted' : 'ok')
      const path = data?.path || null
      if (status === 'rejected') {
        this.statusMessage = `Rejected: ${data?.reason ?? 'outside window'}`
        this.busy = false
        this.update()
        return
      }

      if (status === 'scheduled') {
        this.statusMessage = `Capture scheduled for ${data?.scheduled_for ?? 'future'}`
        this.busy = false
        this.update()
        return
      }

      // accepted/capturing/ok -> poll for the file if path provided
      if (path) {
        const filename = String(path).split('/').pop() || ''
        this.statusMessage = 'Capture accepted — waiting for file to appear...'
        this.update()
        const found = await this.waitForFile(filename, 12, 1000)
        if (found) {
          this.statusMessage = 'Capture completed'
          await this.loadData()
        } else {
          this.statusMessage = 'Capture accepted but file not found yet. Refresh to check.'
        }
      } else {
        // no path returned — just refresh
        await this.loadData()
      }
      this.busy = false
      this.update()
    } catch (err) {
      console.error('[UI] trigger error:', err)
      this.error = err instanceof Error ? err.message : 'Unknown error'
      this.statusMessage = null
      this.busy = false
      this.update()
    }
  }

  private sleep(ms: number) { return new Promise(resolve => setTimeout(resolve, ms)) }

  private async waitForFile(filename: string, attempts = 10, delayMs = 1000) {
    if (!filename) return false
    for (let i = 0; i < attempts; i++) {
      try {
        await this.loadPhotos()
        if (this.photos.find(p => p.id === filename || p.name === filename)) return true
      } catch (_) {}
      await this.sleep(delayMs)
    }
    return false
  }

  private update() {
    const root = document.getElementById('root')
    if (!root) return
    const currentScroll = window.scrollY
    root.innerHTML = this.render()
    this.attachEventListeners()
    window.scrollTo(0, currentScroll)
    // Ensure MJPEG starts when showing Live tab
    if (this.currentTab === 'live') {
      const img = document.getElementById('mjpeg') as HTMLImageElement | null
      if (img && !img.src) img.src = `http://${location.hostname}:8081/`
    }
  }

  private stopMjpeg() {
    const img = document.getElementById('mjpeg') as HTMLImageElement | null
    if (!img) return
    try {
      img.src = ''
      img.remove()
    } catch (e) {
      // ignore DOM removal errors
    }
  }

  // No per-photo selection or preview — Photos tab is download-only

  

  private render() {
    const items = this.photos

    return `
      <div class="page">
        <header class="hero">
          <p class="eyebrow">Camera console</p>
          <div class="hero__row">
            <div>
              <h1>Photo library</h1>
              <p class="subhead">Browse and download photos from your device (download-only).</p>
              ${this.statusMessage ? `<p class="status">${this.statusMessage}</p>` : ''}
            </div>
          </div>
        </header>

        <nav class="tabs">
          <button class="tab ${this.currentTab === 'live' ? 'active' : ''}" id="tab-live">Live</button>
          <button class="tab ${this.currentTab === 'photos' ? 'active' : ''}" id="tab-photos">Photos</button>
        </nav>

        ${this.currentTab === 'live' ? `
          <section class="layout">
            <main class="panel">
              <div class="panel__header"><h2>Live stream</h2>
                <div class="hero__actions">
                  <button class="primary" id="btn-take">📸 Take photo</button>
                </div>
              </div>
              <div class="player__body"><div class="player-grid"><div class="player-preview">
                <img id="mjpeg" src="http://${location.hostname}:8081/" alt="Live stream" style="max-width:100%;height:auto;"/>
              </div></div></div>
            </main>
          </section>
        ` : `
          <section class="layout">
            <main class="panel">
              <div class="panel__header">
                <h2>Available photos</h2>
                <span class="badge">${items.length}</span>
                <div style="float:right">
                  <button class="secondary" id="btn-refresh-photos">Refresh</button>
                </div>
              </div>
              ${this.loading ? '<p class="muted">Loading…</p>' : ''}
              ${this.error ? `<p class="error">${this.error}</p>` : ''}
              ${!this.loading && !this.error && items.length === 0 ? '<p class="muted">No items found.</p>' : ''}
              <ul class="photo-list">
                ${items
                  .map((item) => `
                    <li>
                      <a class="photo-link" href="/photo/${encodeURIComponent(item.id)}">${item.name}</a>
                    </li>
                  `)
                  .join('')}
              </ul>
            </main>
          </section>
        `}
      </div>
    `
  }
}

new App()
