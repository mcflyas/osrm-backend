import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawn, spawnSync } from 'node:child_process';
import { randomUUID } from 'node:crypto';
import { After } from '@cucumber/cucumber';
import { mkBinPath } from './run.js';

export class DeploymentFixture {
  constructor() {
    this.directory = fs.mkdtempSync(path.join(os.tmpdir(), 'osrm profiles ø '));
    this.configPath = path.join(this.directory, 'deployment.json');
    this.output = path.join(this.directory, 'release folder');
    const input = path.join(this.directory, 'map input.osm');
    fs.writeFileSync(input, fs.readFileSync(path.resolve('test/data/multiple_profiles.osm'), 'utf8').replace('</osm>',
      '<way id="20000"><nd ref="1"/><nd ref="256"/><tag k="highway" v="footway"/><tag k="name" v="Foot shortcut"/></way></osm>'));
    this.config = {
      version: 1,
      input,
      output_directory: 'release folder',
      preparation: { threads: 2 },
      server: { threads: 1, ip: '127.0.0.1', port: 0 },
      profiles: [
        { name: 'car', lua: path.resolve('profiles/car.lua'), algorithm: 'CH' },
        { name: 'foot', lua: path.resolve('profiles/foot.lua'), algorithm: 'MLD' }
      ]
    };
    this.processes = [];
    this.namespace = `osrm-test-${randomUUID()}`;
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

  async start(args = ['--config', this.configPath]) {
    const child = spawn(mkBinPath('osrm-routed'), args, { stdio: ['ignore', 'pipe', 'pipe'] });
    this.processes.push(child);
    child.logs = '';
    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error(`Server not ready: ${child.logs}`)), 15000);
      const consume = data => {
        child.logs += data.toString();
        const listening = child.logs.match(/Listening on: .*:(\d+)/);
        if (listening) child.port = Number(listening[1]);
        if (child.port && child.logs.includes('running and waiting for requests')) {
          clearTimeout(timer);
          resolve();
        }
      };
      child.stdout.on('data', consume);
      child.stderr.on('data', consume);
      child.once('error', error => { clearTimeout(timer); reject(error); });
      child.once('exit', () => { clearTimeout(timer); reject(new Error(child.logs)); });
    });
    return child;
  }

  async request(child, route, body) {
    const response = await fetch(`http://127.0.0.1:${child.port}${route}`, body === undefined ? {} : {
      method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body)
    });
    const bytes = Buffer.from(await response.arrayBuffer());
    return { status: response.status, bytes };
  }

  async close() {
    for (const child of this.processes) {
      if (child.exitCode === null && child.signalCode === null) {
        const closed = new Promise(resolve => child.once('close', resolve));
        child.kill('SIGTERM');
        await closed;
      }
    }
    if (this.sharedMemory) {
      const cleanup = this.run('unit_tests/shared-memory-cleanup', [this.namespace]);
      if (cleanup.status !== 0) throw new Error(`Dataset cleanup failed: ${cleanup.stderr}`);
    }
    fs.rmSync(this.directory, { recursive: true, force: true });
  }
}

After(async function () {
  if (this.deployment) await this.deployment.close();
});
