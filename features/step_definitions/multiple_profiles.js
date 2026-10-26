import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { Given, When, Then } from '@cucumber/cucumber';
import { DeploymentFixture } from '../support/multiple_profiles.js';

Given('a deployment configuration with CH car and MLD foot', function () {
  this.deployment = new DeploymentFixture();
});

Given('the second deployment profile has a missing Lua file', function () {
  this.deployment.config.profiles[1].lua = 'missing.lua';
});

Given('the second deployment profile fails during extraction', function () {
  const lua = path.join(this.deployment.directory, 'failing.lua');
  fs.writeFileSync(lua, `api_version = 4
return {
  setup = function() return {properties = {weight_name = 'duration'}} end,
  process_way = function() error('deliberate processing failure') end
}`);
  this.deployment.config.profiles[1].lua = lua;
});

When('I prepare the configured profiles', { timeout: 120000 }, function () {
  this.preparation = this.deployment.prepare();
});

Then('both configured datasets should load independently', function () {
  assert.equal(this.preparation.status, 0, this.preparation.stderr || this.preparation.error?.message);
  const descriptor = JSON.parse(fs.readFileSync(path.join(this.deployment.output, 'prepared.json')));
  assert.equal(descriptor.profiles.length, 2);
  for (const profile of this.deployment.config.profiles) {
    const trial = this.deployment.run('osrm-routed', [path.join(this.deployment.output, profile.name, 'map.osrm'), '-a', profile.algorithm, '--mmap', '--port', '0', '--trial']);
    assert.equal(trial.status, 0, trial.stderr);
  }
});

Then('preparing the same release again should preserve its files and fail', { timeout: 120000 }, function () {
  const file = path.join(this.deployment.output, 'prepared.json');
  const before = fs.readFileSync(file);
  const result = this.deployment.prepare();
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /not empty/);
  assert.deepEqual(fs.readFileSync(file), before);
});

Then('preparation should fail before creating output', function () {
  assert.notEqual(this.preparation.status, 0);
  assert.match(this.preparation.stderr, /foot.*lua/);
  assert.equal(fs.existsSync(this.deployment.output), false);
});

Then('preparation should fail with the profile name and stage', function () {
  assert.notEqual(this.preparation.status, 0);
  assert.match(this.preparation.stderr, /foot.*extract/);
});

Then('the deployment completion descriptor should not exist', function () {
  assert.equal(fs.existsSync(path.join(this.deployment.output, 'prepared.json')), false);
});

Then('prepare help and version should work without a configuration', function () {
  for (const flag of ['--help', '--version']) {
    const result = this.deployment.run('osrm-prepare', [flag]);
    assert.equal(result.status, 0, result.stderr || result.error?.message);
    assert.ok(result.stdout.length > 0);
  }
});

Then('filesystem aliases should be rejected before extraction', { timeout: 120000 }, function () {
  const probe = path.join(this.deployment.directory, 'probe');
  fs.writeFileSync(probe, '');
  if (!fs.existsSync(path.join(this.deployment.directory, 'PROBE'))) return;
  this.deployment.config.profiles[1].name = 'Car';
  const result = this.deployment.prepare();
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /Car.*preflight/);
  assert.deepEqual(fs.readdirSync(path.join(this.deployment.output, 'car')), []);
});
