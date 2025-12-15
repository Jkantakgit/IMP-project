import { useCallback, useEffect, useMemo, useState } from 'react'
import './App.css'

type VideoItem = {
  id: string
  name: string
}

type PhotoItem = {
  id: string
  name: string
}

type Tab = 'videos' | 'photos'

function App() {
  const [tab, setTab] = useState<Tab>('videos')
  const [videos, setVideos] = useState<VideoItem[]>([])
  const [selectedVideoId, setSelectedVideoId] = useState<string | null>(null)
  const [photos, setPhotos] = useState<PhotoItem[]>([])
  const [selectedPhotoId, setSelectedPhotoId] = useState<string | null>(null)
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState<string | null>(null)
  const [takingPhoto, setTakingPhoto] = useState(false)
  const [takingVideo, setTakingVideo] = useState(false)

  const loadVideos = useCallback(async () => {
    setLoading(true)
    setError(null)

    try {
      const response = await fetch('/videos')

      if (!response.ok) {
        throw new Error(`Failed to load videos (${response.status})`)
      }

      const text = await response.text()
      if (!text) {
        setVideos([])
        setSelectedVideoId(null)
        return
      }

      const payload = JSON.parse(text)
      const normalized = (Array.isArray(payload) ? payload : []).map(
        (item, index) => {
          if (typeof item === 'string') {
            return { id: item, name: item }
          }

          if (item && typeof item === 'object') {
            const id = String(item.id ?? item.name ?? index)
            const name = String(item.name ?? item.id ?? `Video ${index + 1}`)
            return { id, name }
          }

          return { id: String(index), name: `Video ${index + 1}` }
        },
      )

      setVideos(normalized)
      setSelectedVideoId((prev) => prev ?? normalized[0]?.id ?? null)
    } catch (err) {
      const message = err instanceof Error ? err.message : 'Unknown error'
      setError(message)
      setVideos([])
      setSelectedVideoId(null)
    } finally {
      setLoading(false)
    }
  }, [])

  const loadPhotos = useCallback(async () => {
    setLoading(true)
    setError(null)

    try {
      const response = await fetch('/photos')

      if (!response.ok) {
        throw new Error(`Failed to load photos (${response.status})`)
      }

      const text = await response.text()
      if (!text) {
        setPhotos([])
        setSelectedPhotoId(null)
        return
      }

      const payload = JSON.parse(text)
      const normalized = (Array.isArray(payload) ? payload : []).map(
        (item, index) => {
          if (typeof item === 'string') {
            return { id: item, name: item }
          }

          if (item && typeof item === 'object') {
            const id = String(item.id ?? item.name ?? index)
            const name = String(item.name ?? item.id ?? `Photo ${index + 1}`)
            return { id, name }
          }

          return { id: String(index), name: `Photo ${index + 1}` }
        },
      )

      setPhotos(normalized)
      setSelectedPhotoId((prev) => prev ?? normalized[0]?.id ?? null)
    } catch (err) {
      const message = err instanceof Error ? err.message : 'Unknown error'
      setError(message)
      setPhotos([])
      setSelectedPhotoId(null)
    } finally {
      setLoading(false)
    }
  }, [])

  const takePhoto = useCallback(async () => {
    setTakingPhoto(true)
    setError(null)

    try {
      const response = await fetch('/photo')

      if (!response.ok) {
        throw new Error(`Failed to take photo (${response.status})`)
      }

      await loadPhotos()
    } catch (err) {
      const message = err instanceof Error ? err.message : 'Unknown error'
      setError(message)
    } finally {
      setTakingPhoto(false)
    }
  }, [loadPhotos])

  const takeVideo = useCallback(async () => {
    setTakingVideo(true)
    setError(null)

    try {
      const response = await fetch('/video')

      if (!response.ok) {
        throw new Error(`Failed to start video recording (${response.status})`)
      }

      await loadVideos()
    } catch (err) {
      const message = err instanceof Error ? err.message : 'Unknown error'
      setError(message)
    } finally {
      setTakingVideo(false)
    }
  }, [loadVideos])

  useEffect(() => {
    if (tab === 'videos') {
      loadVideos()
    } else {
      loadPhotos()
    }
  }, [tab, loadVideos, loadPhotos])

  const selectedVideo = useMemo(
    () => videos.find((video) => video.id === selectedVideoId) ?? null,
    [selectedVideoId, videos],
  )

  const selectedPhoto = useMemo(
    () => photos.find((photo) => photo.id === selectedPhotoId) ?? null,
    [selectedPhotoId, photos],
  )

  return (
    <div className="page">
      <header className="hero">
        <p className="eyebrow">Camera console</p>
        <div className="hero__row">
          <div>
            <h1>{tab === 'videos' ? 'Video' : 'Photo'} library</h1>
            <p className="subhead">
              {tab === 'videos'
                ? 'Browse, preview, and download recordings served from your device.'
                : 'View and download photos captured by your device.'}
            </p>
          </div>
          <div className="hero__actions">
            {tab === 'photos' && (
              <button className="primary" onClick={takePhoto} disabled={takingPhoto}>
                {takingPhoto ? 'Taking…' : '📷 Take photo'}
              </button>
            )}
            {tab === 'videos' && (
              <button className="primary" onClick={takeVideo} disabled={takingVideo}>
                {takingVideo ? 'Recording…' : '🎥 Take video'}
              </button>
            )}
            <button
              className="secondary"
              onClick={tab === 'videos' ? loadVideos : loadPhotos}
              disabled={loading}
            >
              {loading ? 'Refreshing…' : 'Refresh list'}
            </button>
          </div>
        </div>
      </header>

      <nav className="tabs">
        <button
          className={`tab ${tab === 'videos' ? 'active' : ''}`}
          onClick={() => setTab('videos')}
        >
          Videos
        </button>
        <button
          className={`tab ${tab === 'photos' ? 'active' : ''}`}
          onClick={() => setTab('photos')}
        >
          Photos
        </button>
      </nav>

      {tab === 'videos' && (
        <section className="layout">
          <aside className="panel">
            <div className="panel__header">
              <h2>Available videos</h2>
              <span className="badge">{videos.length}</span>
            </div>

            {loading && <p className="muted">Loading videos…</p>}
            {error && <p className="error">{error}</p>}

            {!loading && !error && videos.length === 0 && (
              <p className="muted">No videos found. Try refreshing.</p>
            )}

            <ul className="video-list">
              {videos.map((video) => {
                const isActive = video.id === selectedVideoId
                return (
                  <li key={video.id}>
                    <button
                      className={`video-item ${isActive ? 'active' : ''}`}
                      onClick={() => setSelectedVideoId(video.id)}
                    >
                      <span className="video-name">{video.name}</span>
                      <span className="video-id">{video.id}</span>
                    </button>
                  </li>
                )
              })}
            </ul>
          </aside>

          <main className="panel player">
            <div className="panel__header">
              <h2>Preview</h2>
              {selectedVideo && (
                <a
                  className="secondary"
                  href={`/video/${encodeURIComponent(selectedVideo.id)}`}
                  download
                >
                  Download
                </a>
              )}
            </div>

            {!selectedVideo && (
              <div className="empty-state">
                <p className="muted">Select a video to preview.</p>
              </div>
            )}

            {selectedVideo && (
              <div className="player__body">
                <video
                  key={selectedVideo.id}
                  controls
                  src={`/video/${encodeURIComponent(selectedVideo.id)}`}
                />

                <div className="meta">
                  <div>
                    <p className="eyebrow">Title</p>
                    <p className="meta__value">{selectedVideo.name}</p>
                  </div>
                  <div>
                    <p className="eyebrow">ID</p>
                    <p className="meta__value">{selectedVideo.id}</p>
                  </div>
                </div>
              </div>
            )}
          </main>
        </section>
      )}

      {tab === 'photos' && (
        <section className="layout">
          <aside className="panel">
            <div className="panel__header">
              <h2>Available photos</h2>
              <span className="badge">{photos.length}</span>
            </div>

            {loading && <p className="muted">Loading photos…</p>}
            {error && <p className="error">{error}</p>}

            {!loading && !error && photos.length === 0 && (
              <p className="muted">No photos found. Try taking one!</p>
            )}

            <ul className="video-list">
              {photos.map((photo) => {
                const isActive = photo.id === selectedPhotoId
                return (
                  <li key={photo.id}>
                    <button
                      className={`video-item ${isActive ? 'active' : ''}`}
                      onClick={() => setSelectedPhotoId(photo.id)}
                    >
                      <span className="video-name">{photo.name}</span>
                      <span className="video-id">{photo.id}</span>
                    </button>
                  </li>
                )
              })}
            </ul>
          </aside>

          <main className="panel player">
            <div className="panel__header">
              <h2>Preview</h2>
              {selectedPhoto && (
                <a
                  className="secondary"
                  href={`/photo/${encodeURIComponent(selectedPhoto.id)}`}
                  download
                >
                  Download
                </a>
              )}
            </div>

            {!selectedPhoto && (
              <div className="empty-state">
                <p className="muted">Select a photo to preview.</p>
              </div>
            )}

            {selectedPhoto && (
              <div className="player__body">
                <img
                  key={selectedPhoto.id}
                  src={`/photo/${encodeURIComponent(selectedPhoto.id)}`}
                  alt={selectedPhoto.name}
                />

                <div className="meta">
                  <div>
                    <p className="eyebrow">Title</p>
                    <p className="meta__value">{selectedPhoto.name}</p>
                  </div>
                  <div>
                    <p className="eyebrow">ID</p>
                    <p className="meta__value">{selectedPhoto.id}</p>
                  </div>
                </div>
              </div>
            )}
          </main>
        </section>
      )}
    </div>
  )
}

export default App
