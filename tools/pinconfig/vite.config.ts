import { defineConfig } from 'vitest/config';

// Relative base so the built bundle works from any folder or from GitHub Pages.
export default defineConfig({
    base: './',
    test: {
        include: ['test/**/*.test.ts'],
        testTimeout: 30000,
    },
});
