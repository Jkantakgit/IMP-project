import { defineConfig } from 'vite'
import { viteSingleFile } from 'vite-plugin-singlefile'

// Minimal Vite config (no React plugin)
export default defineConfig({
  plugins: [
    // Inline CSS/JS into the generated HTML to produce a single-file build
    viteSingleFile(),
  ],
})
