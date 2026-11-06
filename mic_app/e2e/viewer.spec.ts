import { test,expect } from '@playwright/test';
test('simulation, trial export, replay and disconnected state',async({page})=>{
  const errors:string[]=[];page.on('pageerror',e=>errors.push(e.message));
  await page.goto('/');
  await expect(page.locator('.status')).toHaveText('Disconnected');
  await page.getByRole('button',{name:'Simulation',exact:true}).click();
  await expect(page.locator('.mode-pill')).toHaveText('SIMULATION');
  await expect(page.locator('.status')).toHaveText('Provisional');
  await page.getByRole('button',{name:'Start trial'}).click();
  await expect(page.getByRole('button',{name:/Finish trial/})).not.toHaveText('■ Finish trial · 0');
  await page.waitForTimeout(1500);
  await page.getByRole('button',{name:/Finish trial/}).click();
  await expect(page.locator('.trial-stats')).toContainText('Acceptance');
  const downloadPromise=page.waitForEvent('download');
  await page.getByRole('button',{name:'Export JSON'}).click();
  const download=await downloadPromise;
  await download.saveAs('../build-tests/mic-app/simulation-session.json');
  await page.getByRole('button',{name:'Replay trial'}).click();
  await expect(page.locator('.mode-pill')).toHaveText('REPLAY');
  await page.getByRole('slider',{name:'Replay position'}).fill('0');
  await expect(page.locator('.angle-readout')).not.toContainText('No usable direction');
  await page.screenshot({path:'../build-tests/mic-app/viewer.png',fullPage:true});
  await page.getByRole('button',{name:'Stop replay'}).click();
  await expect(page.locator('.status')).toHaveText('Disconnected');
  await page.locator('input[type=file]').setInputFiles('../build-tests/mic-app/simulation-session.json');
  await expect(page.locator('.mode-pill')).toHaveText('REPLAY');
  expect(errors).toEqual([]);
});

test('rejects malformed sessions without replacing live simulation',async({page})=>{
  await page.goto('/');await page.getByRole('button',{name:'Simulation',exact:true}).click();
  await page.locator('input[type=file]').setInputFiles({name:'bad.json',mimeType:'application/json',buffer:Buffer.from('{"schema":"invalid"}')});
  await expect(page.getByRole('alert')).toContainText('Unsupported session');
  await expect(page.locator('.mode-pill')).toHaveText('SIMULATION');
});

test('rejected estimates remove the direction arrow and then recover',async({page})=>{
  await page.goto('/');await page.getByRole('button',{name:'Simulation',exact:true}).click();
  await expect(page.locator('.status')).toHaveText('Rejected',{timeout:10000});
  await expect(page.locator('.angle-readout')).toContainText('No usable direction');
  await expect(page.locator('.reason')).toContainText('Insufficient broadband');
  await expect(page.locator('.status')).toHaveText('Provisional',{timeout:3000});
});
