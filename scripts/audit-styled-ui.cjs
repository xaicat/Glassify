'use strict';
// Optional development check. Playwright is never part of the application runtime.
const path = require('path');
const { chromium } = require(
  process.env.GLASSIFY_PLAYWRIGHT_PATH ||
    path.join(__dirname, '../.tools/browser-tools/node_modules/playwright')
);
(async () => {
  const browser = await chromium.connectOverCDP(process.argv[2]);
  try {
    const page = browser
      .contexts()[0]
      .pages()
      .find((p) => p.url().startsWith('https://glassify.local/'));
    if (!page) throw new Error('Glassify page not found');
    const errors = [];
    page.on('pageerror', (e) => errors.push(e.message));
    for (const theme of ['light', 'vscode', 'dark', 'ember']) {
      await page.evaluate((theme) => chrome.webview.postMessage({ action: 'theme', theme }), theme);
      await page.waitForFunction(
        (theme) => document.documentElement.dataset.theme === theme,
        theme
      );
      for (const style of ['minimal', 'glass']) {
        await page.evaluate(
          (style) => chrome.webview.postMessage({ action: 'style', style }),
          style
        );
        await page.waitForFunction(
          (style) => document.documentElement.dataset.style === style,
          style
        );
        for (const width of [1100, 820, 640]) {
          await page.setViewportSize({ width, height: 760 });
          for (const section of [
            'global',
            'windows',
            'presets',
            'focus',
            'automation',
            'taskbar',
            'settings',
          ]) {
            await page.locator(`[data-page="${section}"]`).click();
            const issues = await page.evaluate(() => {
              const bad = [];
              const main = document.querySelector('main');
              if (main.scrollWidth > main.clientWidth + 2) bad.push('horizontal overflow');
              if (
                innerWidth >= 1100 &&
                ['global', 'focus', 'automation', 'taskbar', 'settings'].includes(
                  glassify.getPage()
                ) &&
                main.scrollHeight > main.clientHeight + 2
              )
                bad.push('default-size control page requires vertical scrolling');
              for (const card of document.querySelectorAll('.card')) {
                if (card.scrollWidth > card.clientWidth + 2)
                  bad.push('content clipped inside a card');
              }
              for (const el of document.querySelectorAll('h1,h2,.help-text,.switch-copy small')) {
                if (el.getClientRects().length && parseFloat(getComputedStyle(el).fontSize) < 12)
                  bad.push('small copy: ' + el.textContent.slice(0, 30));
              }
              return bad;
            });
            if (issues.length)
              throw new Error(`${theme}/${style}/${width}/${section}: ${issues.join(', ')}`);
          }
        }
      }
      const iconBounds = await page.locator('#theme-toggle path').evaluate((el) => {
        const b = el.getBBox();
        return { x: b.x, y: b.y, right: b.x + b.width, bottom: b.y + b.height };
      });
      if (iconBounds.x < 0 || iconBounds.y < 0 || iconBounds.right > 24 || iconBounds.bottom > 24)
        throw new Error('Theme icon exceeds its viewbox');
    }
    await page.setViewportSize({ width: 1100, height: 760 });
    await page.locator('[data-page="taskbar"]').click();
    const ready = await page.evaluate(() => glassify.getState().taskbarInstalled);
    if (!ready && !(await page.locator('[data-action="applyTaskbar"]').isDisabled()))
      throw new Error('Apply enabled before setup');
    await page.locator('[data-page="settings"]').focus();
    await page.keyboard.press('Tab');
    const ring = await page.evaluate(() => {
      const s = getComputedStyle(document.activeElement);
      return s.outlineStyle === 'solid' && parseFloat(s.outlineWidth) >= 2;
    });
    if (!ring) throw new Error('Keyboard focus needs a visible solid ring');
    if (errors.length) throw new Error(errors.join('\n'));
    console.log(
      'PASS: Playwright, seven pages, four color themes, two styles, three widths, compact default pages, icon bounds and keyboard focus.'
    );
  } finally {
    await browser.close();
  }
})().catch((e) => {
  console.error(e);
  process.exitCode = 1;
});
