const {spawnSync} = require('node:child_process');
const {readdirSync} = require('node:fs');
for (const name of readdirSync(__dirname).filter(name => name.endsWith('-tests.cjs')).sort()) {
    if (name === 'run-browser-tests.cjs') continue;
    console.log(`Running ${name}`);
    const result = spawnSync(process.execPath, [require('node:path').join(__dirname, name)], {stdio: 'inherit'});
    if (result.status !== 0) process.exit(result.status || 1);
}
