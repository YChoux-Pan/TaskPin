import { defineConfig } from '@playwright/test';
import path from 'node:path';

export default defineConfig({
  testDir: '.',
  testMatch: 'native.spec.ts',
  workers: 1,
  fullyParallel: false,
  timeout: 180_000,
  retries: 0,
  outputDir: path.resolve('docs/artifacts/playwright/results'),
  reporter: [
    ['list'],
    ['html', { outputFolder: path.resolve('docs/artifacts/playwright/html'), open: 'never' }],
    ['junit', { outputFile: path.resolve('docs/artifacts/playwright/junit.xml') }],
    ['json', { outputFile: path.resolve('docs/artifacts/playwright/results.json') }],
  ],
});
