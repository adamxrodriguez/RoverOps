import { test, expect } from '@playwright/test';

test('operator commands change the actual C++ rover and render live telemetry', async ({
  page,
}) => {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  await page.goto('/');
  await expect(page.getByRole('status').filter({ hasText: /^Live telemetry$/ })).toBeVisible();
  await page.getByLabel('X position', { exact: true }).fill('8');
  await page.getByLabel('Y position', { exact: true }).fill('5');
  await page.getByRole('button', { name: 'Set waypoint', exact: true }).click();
  await page.getByRole('button', { name: 'Start', exact: true }).click();
  await expect(page.getByTestId('mode')).toHaveText('navigating');
  await expect(page.getByTestId('velocity')).not.toHaveText('0.00 m/s');
  await page.getByLabel('Target velocity', { exact: true }).fill('1.5');
  await page.getByRole('button', { name: 'Set velocity', exact: true }).click();
  await expect(page.getByText('Target 1.50 m/s', { exact: true })).toBeVisible();
  await page.getByRole('button', { name: 'Pause', exact: true }).click();
  await expect(page.getByTestId('mode')).toHaveText('paused');
  await expect(page.getByTestId('velocity')).toHaveText('0.00 m/s');
  await page.getByRole('button', { name: 'Resume', exact: true }).click();
  await expect(page.getByTestId('mode')).toHaveText('navigating');
  await page.waitForTimeout(1800);
  await page.screenshot({ path: '../docs/screenshots/dashboard.png', fullPage: true });
  await page.getByRole('button', { name: /Emergency stop/ }).click();
  await expect(page.getByTestId('mode')).toHaveText('emergency stopped');
  await expect(page.getByRole('button', { name: 'Start', exact: true })).toBeDisabled();
  await expect(page.getByRole('button', { name: 'Return home', exact: true })).toBeDisabled();
  const stoppedPosition = await page.getByTestId('position').textContent();
  await page.waitForTimeout(350);
  await expect(page.getByTestId('position')).toHaveText(stoppedPosition!);
  await page.getByRole('button', { name: 'Resume', exact: true }).click();
  await page.getByRole('button', { name: 'Return home', exact: true }).click();
  await expect(page.getByTestId('mode')).toHaveText('arrived', { timeout: 15_000 });
  expect(errors).toEqual([]);
});

test('mobile layout fits and unavailable telemetry disables motion commands', async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 844 });
  await page.routeWebSocket('**/ws/telemetry', (ws) => {
    ws.close();
  });
  await page.goto('/');
  await expect(page.getByText('Disconnected · reconnecting', { exact: true })).toBeVisible();
  await expect(page.getByRole('button', { name: 'Start', exact: true })).toBeDisabled();
  await expect(page.getByRole('button', { name: /Emergency stop/ })).toBeEnabled();
  const overflow = await page.evaluate(() => document.documentElement.scrollWidth > innerWidth);
  expect(overflow).toBe(false);
});

test('dashboard reconnects after a transport close and resumes telemetry', async ({ page }) => {
  let connections = 0;
  await page.routeWebSocket('**/ws/telemetry', (ws) => {
    connections++;
    const server = ws.connectToServer();
    if (connections === 1) setTimeout(() => server.close(), 500);
  });
  await page.goto('/');
  await expect.poll(() => connections).toBeGreaterThan(1);
  await expect(page.getByRole('status').filter({ hasText: /^Live telemetry$/ })).toBeVisible();
});
