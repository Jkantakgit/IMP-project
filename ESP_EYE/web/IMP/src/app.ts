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
  // device_time_ms - local Date.now()
  private clientTimeOffsetMs: number | null = null

  constructor() {
    this.init()
  }

  private async init() {
    document.getElementById('root')!.innerHTML = this.render()
    this.attachEventListeners()
    // Sync device time and learn device offset on page load so reloads work
    void this.syncTime()
    try { await this.fetchDeviceTime() } catch (_) {}
  }

  private attachEventListeners() {
    document.getElementById('tab-live')?.addEventListener('click', () => {
      if (this.currentTab === 'live') return
      this.currentTab = 'live'
      // When switching to Live, ask the device to sync its clock
      void this.syncTime()
      this.update()
    })
    document.getElementById('tab-photos')?.addEventListener('click', () => {
      if (this.currentTab === 'photos') return
      this.stopMjpeg()
      this.currentTab = 'photos'
      this.update()
    })
    document.getElementById('btn-take')?.addEventListener('click', () => { if (!this.busy) void this.takeMedia() })
    document.getElementById('btn-refresh-photos')?.addEventListener('click', () => { void this.loadPhotos() })
  }



  private async fetchPhotosList() {
    const res = await fetchWithTimeout('/photos', { method: 'GET', headers: { Accept: 'application/json' } }, 10000)
    if (!res.ok) throw new Error(`Failed (${res.status})`)
    const text = await res.text()
    if (!text) return []
    const data = JSON.parse(text)
    const list = Array.isArray((data as any).files)
      ? (data as any).files
      : Array.isArray(data)
      ? data
      : []
    // Normalize to array of filename strings
    return list.map((item: any, i: number) => (typeof item === 'string' ? item : String(item.name ?? item.id ?? i)))
  }

  private _photosCache: { ts: number; list: string[] } | null = null

  private async syncTime() {
    try {
      this.statusMessage = 'Syncing device time…'
      this.update()
      const payload = { time_ms: Date.now() }
      const res = await fetchWithTimeout('/time', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(payload) }, 5000)
      if (!res.ok) throw new Error(`Failed to sync (${res.status})`)
      // after posting, refresh device time offset
      try { await this.fetchDeviceTime() } catch (_) {}
      // briefly show success
      this.statusMessage = 'Device time synced'
      this.update()
      await this.sleep(800)
      this.statusMessage = null
      this.update()
    } catch (err) {
      this.statusMessage = err instanceof Error ? `Time sync failed: ${err.message}` : 'Time sync failed'
      this.update()
      // keep the message for a short time so user notices
      await this.sleep(1500)
      this.statusMessage = null
      this.update()
    }
  }

  private async fetchDeviceTime() {
    const timeRes = await fetchWithTimeout('/time', { method: 'GET', headers: { Accept: 'application/json' } }, 5000)
    if (!timeRes.ok) throw new Error(`Failed to get time (${timeRes.status})`)
    const timeJson = await timeRes.json().catch(() => null)
    const deviceTime = timeJson?.time_ms
    if (typeof deviceTime === 'number') {
      this.clientTimeOffsetMs = deviceTime - Date.now()
    } else {
      throw new Error('Invalid /time response')
    }
  }

  private async loadPhotos() {
    this.loading = true
    this.error = null
    this.statusMessage = null
    this.update()

    try {
      const list = await this.fetchPhotosList()
      // Map to MediaItem and create human-readable name: replace 'x'->' ' and '_'->':'
      this.photos = list.map((fname: string) => {
        const id = String(fname)
        const base = id.replace(/\.jpg$/i, '')
        const display = base.replace(/x/g, ' ').replace(/_/g, ':')
        return { id, name: display }
      })
      // Deduplicate entries
      this.photos = this.photos.filter((p, idx, arr) => arr.findIndex(x => x.id === p.id) === idx)
    } catch (err) {
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
      this.busy = true
      this.statusMessage = 'Fetching device time…'
      this.update()

      // Determine device-aligned timestamp to send. Prefer cached offset
      // so reloads behave consistently; fall back to querying /time.
      let deviceTime: number
      if (this.clientTimeOffsetMs !== null) {
        deviceTime = Date.now() + this.clientTimeOffsetMs
      } else {
        const timeRes = await fetchWithTimeout('/time', { method: 'GET', headers: { Accept: 'application/json' } }, 5000)
        if (!timeRes.ok) throw new Error(`Failed to get time (${timeRes.status})`)
        const timeJson = await timeRes.json().catch(() => null)
        deviceTime = timeJson?.time_ms ?? Date.now()
        if (typeof deviceTime === 'number') this.clientTimeOffsetMs = deviceTime - Date.now()
      }

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
          // Only refresh UI photos if user is viewing the Photos tab
          if (this.currentTab === 'photos') await this.loadPhotos()
        } else {
          this.statusMessage = 'Capture accepted but file not found yet. Refresh to check.'
        }
      } else {
        // no path returned — just refresh
        if (this.currentTab === 'photos') await this.loadPhotos()
      }
      this.busy = false
      this.update()
    } catch (err) {
      this.error = err instanceof Error ? err.message : 'Unknown error'
      this.statusMessage = null
      this.busy = false
      this.update()
    }
  }

  private sleep(ms: number) { return new Promise(resolve => setTimeout(resolve, ms)) }

  private async waitForFile(filename: string, attempts = 6, delayMs = 2000) {
    if (!filename) return false
    for (let i = 0; i < attempts; i++) {
      try {
        // honor cache to reduce load
        const now = Date.now()
        let list: string[]
        if (this._photosCache && now - this._photosCache.ts < 5000) {
          list = this._photosCache.list
        } else {
          list = await this.fetchPhotosList()
          this._photosCache = { ts: now, list }
        }
        if (list.find((f: string) => f === filename)) return true
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
    } catch (e) {}
  }

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
