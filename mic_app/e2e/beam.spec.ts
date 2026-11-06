import {test,expect} from '@playwright/test';
test('manual beam controls, marker, profile reset and replay isolation',async({page})=>{
 await page.goto('/');await expect(page.getByRole('button',{name:'Apply beam',exact:true})).toBeDisabled();
 await page.getByRole('button',{name:'Simulation',exact:true}).click();
 await page.getByLabel('Beam mode',{exact:true}).selectOption('2');
 await page.getByLabel('Beam azimuth',{exact:true}).fill('135');await page.getByLabel('Beam elevation',{exact:true}).fill('20');
 await page.getByRole('button',{name:'Apply beam',exact:true}).click();
 await expect(page.getByTestId('beam-applied')).toContainText('135.0° azimuth · 20.0° elevation');
 await expect(page.getByTestId('beam-marker')).toHaveAttribute('transform','rotate(-135 210 210)');
 await expect(page.locator('.beam-controls')).toContainText('Relearn after steering');
 await page.getByRole('button',{name:'Relearn noise',exact:true}).click();
 await expect(page.locator('.beam-controls')).toContainText('Learned',{timeout:4000});
 await page.getByLabel('Beam azimuth',{exact:true}).fill('360');await expect(page.getByRole('button',{name:'Apply beam',exact:true})).toBeDisabled();
 await page.getByLabel('Beam azimuth',{exact:true}).fill('0');await page.getByRole('button',{name:'Use current estimate',exact:true}).click();
 await expect(page.getByLabel('Beam azimuth',{exact:true})).not.toHaveValue('0');
 await page.getByRole('button',{name:'Stop simulation',exact:true}).click();await expect(page.getByRole('button',{name:'Apply beam',exact:true})).toBeDisabled();
});

test('noise suppression and saved settings controls',async({page})=>{
 await page.goto('/');await expect(page.getByRole('button',{name:'Enable suppression',exact:true})).toBeDisabled();
 await page.getByRole('button',{name:'Simulation',exact:true}).click();
 await page.getByRole('button',{name:'Disable suppression',exact:true}).click();
 await expect(page.locator('.noise-settings')).toContainText('Off');
 await expect(page.getByRole('button',{name:'Relearn noise',exact:true})).toBeDisabled();
 await page.getByRole('button',{name:'Enable suppression',exact:true}).click();
 await expect(page.locator('.noise-settings')).toContainText('Enabled');
 await page.getByRole('button',{name:'Relearn noise',exact:true}).click();
 await expect(page.getByRole('button',{name:'Save & restart',exact:true})).toBeDisabled();
 await expect(page.getByRole('button',{name:'Save & restart',exact:true})).toBeEnabled({timeout:4000});
 await page.getByRole('button',{name:'Save & restart',exact:true}).click();
 await expect(page.locator('.persistence')).toContainText('Audio settings saved on board');
 await page.getByRole('button',{name:'Disable suppression',exact:true}).click();
 await expect(page.locator('.persistence')).toContainText('Unsaved audio settings');
});
