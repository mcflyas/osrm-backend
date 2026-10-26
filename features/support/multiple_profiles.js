import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { After } from '@cucumber/cucumber';
import { mkBinPath } from './run.js';

export class DeploymentFixture {
  constructor() {
    this.directory = fs.mkdtempSync(path.join(os.tmpdir(), 'osrm profiles ø '));
    this.configPath = path.join(this.directory, 'deployment.json');
    this.output = path.join(this.directory, 'release folder');
    this.config = {
      version: 1,
      input: path.resolve('test/data/multiple_profiles.osm'),
      output_directory: 'release folder',
      preparation: { threads: 2 },
      server: { threads: 1, ip: '127.0.0.1', port: 0 },
      profiles: [
        { name: 'car', lua: path.resolve('profiles/car.lua'), algorithm: 'CH' },
        { name: 'foot', lua: path.resolve('profiles/foot.lua'), algorithm: 'MLD' }
      ]
    };
    this.processes = [];
  }

  writeConfig() {
    fs.writeFileSync(this.configPath, JSON.stringify(this.config));
  }

  run(binary, args = []) {
    return spawnSync(mkBinPath(binary), args, { encoding: 'utf8', timeout: 60000, maxBuffer: 8 * 1024 * 1024 });
  }

  prepare() {
    this.writeConfig();
    return this.run('osrm-prepare', ['--config', this.configPath]);
  }

  async close() {
    for (const child of this.processes) {
      if (child.exitCode === null && child.signalCode === null) {
        const closed = new Promise(resolve => child.once('close', resolve));
        child.kill('SIGTERM');
        await closed;
      }
    }
    fs.rmSync(this.directory, { recursive: true, force: true });
  }
}

After(async function () {
  if (this.deployment) await this.deployment.close();
});
