import './App.css'

function fetchWithTimeout(input: RequestInfo | URL, init?: RequestInit, timeout = 8000) {
  const controller = new AbortController()
  const id = setTimeout(() => controller.abort(), timeout)
  const finalInit: RequestInit = { ...(init || {}), signal: controller.signal }
  return fetch(input, finalInit)
    .finally(() => clearTimeout(id))
}

type Tab = 'videos' | 'photos'
type MediaItem = { id: string; name: string }

class App {
  private tab: Tab = 'videos'
  private videos: MediaItem[] = []
  private photos: MediaItem[] = []
  private selectedVideoId: string | null = null
  private selectedPhotoId: string | null = null
  private loading = false
  private error: string | null = null

  constructor() {
    this.init()
  }

  private async init() {
    document.getElementById('root')!.innerHTML = this.render()
    this.attachEventListeners()
    await this.loadData()
  }

  private attachEventListeners() {
    document.getElementById('tab-videos')?.addEventListener('click', () => this.switchTab('videos'))
    document.getElementById('tab-photos')?.addEventListener('click', () => this.switchTab('photos'))
    document.getElementById('btn-refresh')?.addEventListener('click', () => this.loadData())
    document.getElementById('btn-take')?.addEventListener('click', () => this.takeMedia())
  }

  private async switchTab(tab: Tab) {
    this.tab = tab
    this.update()
    await this.loadData()
  }

  private async loadData() {
    if (this.tab === 'videos') await this.loadVideos()
    else await this.loadPhotos()
  }

  private async loadVideos() {
    this.loading = true
    this.error = null
    this.update()

    try {
      console.log('[UI] Fetching /videos …')
      const res = await fetchWithTimeout('/videos', undefined, 10000)
      if (!res.ok) throw new Error(`Failed (${res.status})`)
      const text = await res.text()
      if (!text) {
        this.videos = []
        this.selectedVideoId = null
        return
      }
      const data = JSON.parse(text)
      const list = Array.isArray((data as any).files)
        ? (data as any).files
        : Array.isArray(data)
        ? data
        : []
      console.log('[UI] /videos parsed list', list)
      this.videos = list.map((item, i) =>
        typeof item === 'string'
          ? { id: item, name: item }
          : { id: String(item.name ?? item.id ?? i), name: String(item.name ?? item.id ?? `Video ${i + 1}`) }
      )
      this.selectedVideoId = this.selectedVideoId ?? this.videos[0]?.id ?? null
    } catch (err) {
      console.error('[UI] /videos error:', err)
      this.error = err instanceof Error ? err.message : 'Unknown error'
      this.videos = []
      this.selectedVideoId = null
    } finally {
      this.loading = false
      this.update()
    }
  }

  private async loadPhotos() {
    this.loading = true
    this.error = null
    this.update()

    try {
      console.log('[UI] Fetching /photos …')
      const res = await fetchWithTimeout('/photos', undefined, 10000)
      if (!res.ok) throw new Error(`Failed (${res.status})`)
      const text = await res.text()
      if (!text) {
        this.photos = []
        this.selectedPhotoId = null
        return
      }
      const data = JSON.parse(text)
      const list = Array.isArray((data as any).files)
        ? (data as any).files
        : Array.isArray(data)
        ? data
        : []
      console.log('[UI] /photos parsed list', list)
      this.photos = list.map((item, i) =>
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
    const endpoint = this.tab === 'videos' ? '/video' : '/photo'
    try {
      console.log('[UI] Trigger', endpoint)
      const res = await fetchWithTimeout(endpoint, { method: 'POST' }, 15000)
      if (!res.ok) throw new Error(`Failed (${res.status})`)
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
    document.querySelectorAll('[data-video-id]').forEach((el) =>
      el.addEventListener('click', () => {
        this.selectedVideoId = (el as HTMLElement).dataset.videoId!
        this.update()
      })
    )
    document.querySelectorAll('[data-photo-id]').forEach((el) =>
      el.addEventListener('click', () => {
        this.selectedPhotoId = (el as HTMLElement).dataset.photoId!
        this.update()
      })
    )
  }

  private render() {
    const items = this.tab === 'videos' ? this.videos : this.photos
    const selectedId = this.tab === 'videos' ? this.selectedVideoId : this.selectedPhotoId
    const selected = items.find((i) => i.id === selectedId)

    return `
      <div class="page">
        <header class="hero">
          <p class="eyebrow">Camera console</p>
          <div class="hero__row">
            <div>
              <h1>${this.tab === 'videos' ? 'Video' : 'Photo'} library</h1>
              <p class="subhead">${
                this.tab === 'videos'
                  ? 'Browse, preview, and download recordings served from your device.'
                  : 'View and download photos captured by your device.'
              }</p>
            </div>
            <div class="hero__actions">
              <button class="primary" id="btn-take">${
                this.tab === 'videos' ? '🎥 Take video' : '📷 Take photo'
              }</button>
              <button class="secondary" id="btn-refresh" ${this.loading ? 'disabled' : ''}>${this.loading ? 'Refreshing…' : 'Refresh'}</button>
            </div>
          </div>
        </header>

        <nav class="tabs">
          <button class="tab ${this.tab === 'videos' ? 'active' : ''}" id="tab-videos">Videos</button>
          <button class="tab ${this.tab === 'photos' ? 'active' : ''}" id="tab-photos">Photos</button>
        </nav>

        <section class="layout">
          <aside class="panel">
            <div class="panel__header">
              <h2>Available ${this.tab}</h2>
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
                  <button class="video-item ${item.id === selectedId ? 'active' : ''}" 
                    data-${this.tab === 'videos' ? 'video' : 'photo'}-id="${item.id}">
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
              ${
                selected
                  ? `<a class="secondary" href="/${this.tab === 'videos' ? 'video' : 'photo'}/${encodeURIComponent(
                      selected.id
                    )}" download>Download</a>`
                  : ''
              }
            </div>
            ${
              !selected
                ? '<div class="empty-state"><p class="muted">Select an item to preview.</p></div>'
                : `
              <div class="player__body">
                ${
                  this.tab === 'videos'
                    ? `<video controls src="/video/${encodeURIComponent(selected.id)}"></video>`
                    : `<img src="/photo/${encodeURIComponent(selected.id)}" alt="${selected.name}">`
                }
                <div class="meta">
                  <div><p class="eyebrow">Title</p><p class="meta__value">${selected.name}</p></div>
                  <div><p class="eyebrow">ID</p><p class="meta__value">${selected.id}</p></div>
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
