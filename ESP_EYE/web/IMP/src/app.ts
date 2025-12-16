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
  private selectedPhotoId: string | null = null
  private loading = false
  private error: string | null = null
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
    document.getElementById('btn-refresh')?.addEventListener('click', () => this.loadData())
    document.getElementById('btn-take')?.addEventListener('click', () => this.takeMedia())
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
        this.selectedPhotoId = null
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
      this.selectedPhotoId = this.selectedPhotoId ?? this.photos[0]?.id ?? null
    } catch (err) {
      console.error('[UI] /photos error:', err)
      this.error = err instanceof Error ? err.message : 'Unknown error'
      this.photos = []
      this.selectedPhotoId = null
    } finally {
      this.loading = false
      this.update()
    }
  }

  private async takeMedia() {
    const endpoint = '/photo'
    try {
      console.log('[UI] Trigger', endpoint)
      const res = await fetchWithTimeout(endpoint, { method: 'POST' }, 15000)
      if (res.status === 202) {
        // Server accepted recording request and is busy recording.
        this.statusMessage = 'Capture started — the device is busy. File will appear after capture finishes.'
        this.update()
        return
      }
      if (!res.ok) throw new Error(`Failed (${res.status})`)
      // On successful immediate response, refresh list
      await this.loadData()
    } catch (err) {
      console.error('[UI] trigger error:', err)
      this.error = err instanceof Error ? err.message : 'Unknown error'
      this.update()
    }
  }

  private update() {
    const root = document.getElementById('root')
    if (!root) return
    const currentScroll = window.scrollY
    root.innerHTML = this.render()
    this.attachEventListeners()
    this.attachMediaListeners()
    window.scrollTo(0, currentScroll)
  }

  private attachMediaListeners() {
    document.querySelectorAll('[data-photo-id]').forEach((el) =>
      el.addEventListener('click', () => {
        this.selectedPhotoId = (el as HTMLElement).dataset.photoId!
        this.update()
      })
    )
  }

  private render() {
    const items = this.photos
    const selectedId = this.selectedPhotoId
    const selected = items.find((i) => i.id === selectedId)

    return `
      <div class="page">
        <header class="hero">
          <p class="eyebrow">Camera console</p>
          <div class="hero__row">
            <div>
              <h1>Video library</h1>
              <p class="subhead">Browse and download recordings from your device (download-only).</p>
              ${this.statusMessage ? `<p class="status">${this.statusMessage}</p>` : ''}
            </div>
            <div class="hero__actions">
              <button class="primary" id="btn-take">📸 Take photo</button>
              <button class="secondary" id="btn-refresh" ${this.loading ? 'disabled' : ''}>${this.loading ? 'Refreshing…' : 'Refresh'}</button>
            </div>
          </div>
        </header>

        <nav class="tabs">
          <button class="tab active" id="tab-photos">Photos</button>
        </nav>

        <section class="layout">
          <aside class="panel">
            <div class="panel__header">
              <h2>Available photos</h2>
              <span class="badge">${items.length}</span>
            </div>
            ${this.loading ? '<p class="muted">Loading…</p>' : ''}
            ${this.error ? `<p class="error">${this.error}</p>` : ''}
            ${!this.loading && !this.error && items.length === 0 ? '<p class="muted">No items found.</p>' : ''}
            <ul class="video-list">
              ${items
                .map(
                  (item) => `
                <li>
                  <button class="video-item ${item.id === selectedId ? 'active' : ''}" data-photo-id="${item.id}">
                    <span class="video-name">${item.name}</span>
                    <span class="video-id">${item.id}</span>
                  </button>
                </li>
              `
                )
                .join('')}
            </ul>
          </aside>

          <main class="panel">
            <div class="panel__header">
              <h2>Preview</h2>
              ${selected ? `<a class="primary" href="/photo/${encodeURIComponent(selected.id)}">Open</a>` : ''}
            </div>
            ${
              !selected
                ? '<div class="empty-state"><p class="muted">Select a photo to preview or open.</p></div>'
                : `
              <div class="player__body">
                <div class="player-grid">
                  <div class="player-preview">
                    <img src="/photo/${encodeURIComponent(selected.id)}" alt="${selected.name}" />
                  </div>
                  <div class="player-actions">
                    <a class="primary large" href="/photo/${encodeURIComponent(selected.id)}">Open full</a>
                    <div class="meta" style="margin-top:12px">
                      <div><p class="eyebrow">Title</p><p class="meta__value">${selected.name}</p></div>
                      <div><p class="eyebrow">ID</p><p class="meta__value">${selected.id}</p></div>
                    </div>
                  </div>
                </div>
              </div>
            `
            }
          </main>
        </section>
      </div>
    `
  }
}

new App()
