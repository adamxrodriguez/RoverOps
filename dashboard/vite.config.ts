import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

const target = process.env.ROVEROPS_API_URL ?? 'http://127.0.0.1:8080';
const proxy = {
  '/api': { target },
  '/ws': { target, ws: true },
};

export default defineConfig({
  plugins: [react()],
  server: { proxy },
  preview: { proxy },
});
