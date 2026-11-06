import { defineConfig } from '@playwright/test';
export default defineConfig({
  testDir:'./e2e',
  use:{baseURL:'http://127.0.0.1:1420',channel:'msedge',viewport:{width:1380,height:1050}},
  webServer:{command:'npm run dev -- --host 127.0.0.1',url:'http://127.0.0.1:1420',reuseExistingServer:!process.env.CI},
  reporter:'list',
});
