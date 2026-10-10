import { defineConfig } from '@playwright/test';

export default defineConfig({
  testDir: './tests',
  timeout: 30_000,
  fullyParallel: false,
  workers: 1,
  use: {
    baseURL: 'http://127.0.0.1:5173',
    viewport: { width: 1440, height: 1200 },
    trace: 'retain-on-failure',
  },
  webServer: [
    {
      command: '../build/roverops --config ../config/rover.json --port 18081',
      url: 'http://127.0.0.1:18081/api/health',
      reuseExistingServer: false,
      timeout: 60_000,
    },
    {
      command: 'npm run dev',
      env: { ROVEROPS_API_URL: 'http://127.0.0.1:18081' },
      url: 'http://127.0.0.1:5173',
      reuseExistingServer: false,
      timeout: 15_000,
    },
  ],
});
