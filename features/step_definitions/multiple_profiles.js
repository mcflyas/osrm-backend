import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { Given, When, Then } from '@cucumber/cucumber';
import { DeploymentFixture } from '../support/multiple_profiles.js';

Given('a deployment configuration with CH car and MLD foot', function () {
  this.deployment = new DeploymentFixture();
});

Given('the deployment uses its own shared-memory namespace', function () {
  this.deployment.sharedMemory = true;
  Object.assign(this.deployment.config.server, { storage: 'shared_memory', dataset_namespace: this.deployment.namespace });
});

Given('an isolated legacy datastore namespace', function () {
  this.deployment = new DeploymentFixture();
  this.deployment.sharedMemory = true;
});

Then('stdout should contain the isolated dataset regions', function () {
  assert.ok(this.stdout.includes(`${this.deployment.namespace}/legacy/static`));
  assert.ok(this.stdout.includes(`${this.deployment.namespace}/legacy/updatable`));
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

Then('every deployment service should match its standalone engine', { timeout: 120000 }, async function () {
  assert.equal(this.preparation.status, 0, this.preparation.stderr);
  const fixture = this.deployment;
  const combined = await fixture.start();
  const coordinates = '7,43;7.006,43;7.012,43';
  const expectedRoutes = [];
  for (const profile of fixture.config.profiles) {
    const standalone = await fixture.start([path.join(fixture.output, profile.name, 'map.osrm'), '-a', profile.algorithm, '--port', '0', '--threads', '1', '--mmap']);
    for (const service of ['route', 'table', 'nearest', 'match', 'trip', 'tile']) {
      const query = service === 'tile' ? 'tile(17021,12039,15).mvt' : service === 'nearest' ? '7,43' : coordinates;
      const expected = await fixture.request(standalone, `/${service}/v1/arbitrary/${query}`);
      const actual = await fixture.request(combined, `/${service}/v1/${profile.name}/${query}`);
      assert.equal(expected.status, 200, `${service}: ${expected.bytes}`);
      assert.deepEqual(actual, expected, `${profile.name} ${service}`);
      if (service === 'route') expectedRoutes.push(actual);
      if (['route', 'table', 'nearest', 'match'].includes(service)) {
        const body = { coordinates: service === 'nearest' ? [[7, 43]] : [[7, 43], [7.006, 43], [7.012, 43]] };
        const postExpected = await fixture.request(standalone, `/${service}/v1/arbitrary`, body);
        const postActual = await fixture.request(combined, `/${service}/v1/${profile.name}`, body);
        assert.equal(postExpected.status, 200, `${service}: ${postExpected.bytes}`);
        assert.deepEqual(postActual, postExpected, `${profile.name} ${service} POST`);
      }
    }
  }
  assert.notDeepEqual(expectedRoutes[0], expectedRoutes[1]);
  for (let i = 0; i < 12; i++) {
    const selected = i % 2;
    assert.deepEqual(await fixture.request(combined, `/route/v1/${fixture.config.profiles[selected].name}/${coordinates}`), expectedRoutes[selected]);
  }
  const missing = await fixture.request(combined, `/route/v1/missing/${coordinates}`);
  assert.equal(missing.status, 400);
  assert.equal(JSON.parse(missing.bytes).code, 'InvalidProfile');
});

Then('deployment server options should preserve defaults and honor overrides', { timeout: 30000 }, async function () {
  const fixture = this.deployment;
  Object.assign(fixture.config.server, { port: 5101, max_table_size: 123, max_header_size: 0, max_viaroute_size: 3, max_trip_size: 3, max_matching_size: 3 });
  fixture.writeConfig();
  const original = fixture.run('osrm-routed', ['--config', fixture.configPath, '--trial']);
  const overridden = fixture.run('osrm-routed', ['--config', fixture.configPath, '--port', '5102', '--max-table-size', '456', '--trial']);
  assert.equal(original.status, 0, original.stderr);
  assert.equal(overridden.status, 0, overridden.stderr);
  const logs = original.stdout + original.stderr;
  const newLogs = overridden.stdout + overridden.stderr;
  assert.match(logs, /IP port: 5101/);
  assert.match(newLogs, /IP port: 5102/);
  assert.ok(Number(newLogs.match(/Maximum header size: (\d+)/)[1]) > Number(logs.match(/Maximum header size: (\d+)/)[1]));
  assert.doesNotMatch(newLogs, /running and waiting/);
  const configured = await fixture.start(['--config', fixture.configPath, '--port', '0']);
  const explicit = await fixture.start(['--config', fixture.configPath, '--port', '0', '--max-table-size', '456']);
  const body = { coordinates: Array.from({ length: 124 }, () => [7, 43]) };
  const tooMany = await fixture.request(configured, '/table/v1/car', body);
  assert.equal(JSON.parse(tooMany.bytes).code, 'TooBig');
  assert.equal((await fixture.request(explicit, '/table/v1/car', body)).status, 200);
});

Then('incomplete or incompatible releases should fail before readiness', function () {
  const fixture = this.deployment;
  const descriptor = path.join(fixture.output, 'prepared.json');
  const original = fs.readFileSync(descriptor);
  const trial = () => fixture.run('osrm-routed', ['--config', fixture.configPath, '--trial']);
  for (const invalid of ['{}', '{"version":1,"profiles":[]}']) {
    fs.writeFileSync(descriptor, invalid);
    assert.notEqual(trial().status, 0);
  }
  fs.writeFileSync(descriptor, original);
  fixture.config.profiles[1].algorithm = 'CH';
  fixture.writeConfig();
  assert.notEqual(trial().status, 0);
  fixture.config.profiles[1].algorithm = 'MLD';
  fixture.writeConfig();
  const properties = path.join(fixture.output, 'foot/map.osrm.properties');
  const validProperties = fs.readFileSync(properties);
  fs.writeFileSync(properties, 'broken artifact');
  const corrupt = trial();
  assert.notEqual(corrupt.status, 0);
  assert.match(corrupt.stderr, /Profile foot/);
  assert.doesNotMatch(corrupt.stdout + corrupt.stderr, /running and waiting/);
  fs.writeFileSync(properties, validProperties);
  fs.rmSync(path.join(fixture.output, 'foot/map.osrm.mldgr'));
  const missing = trial();
  assert.notEqual(missing.status, 0);
  assert.match(missing.stderr, /foot/);
  assert.doesNotMatch(missing.stdout + missing.stderr, /running and waiting/);
});

Then('a serving-only deployment configuration should initialize', function () {
  const fixture = this.deployment;
  delete fixture.config.input;
  for (const profile of fixture.config.profiles) delete profile.lua;
  fixture.writeConfig();
  const result = fixture.run('osrm-routed', ['--config', fixture.configPath, '--trial']);
  assert.equal(result.status, 0, result.stderr);
});

Then('deployment configuration should reject legacy dataset flags', function () {
  const fixture = this.deployment;
  for (const flags of [['anything.osrm'], ['--algorithm', 'CH'], ['--dataset-name', 'x'], ['--shared-memory'], ['--mmap'], ['--memory_file', 'x']]) {
    const result = fixture.run('osrm-routed', ['--config', fixture.configPath, ...flags, '--trial']);
    assert.notEqual(result.status, 0, flags.join(' '));
    assert.match(result.stderr, /conflict|cannot.*combine/i);
  }
});

function publish(fixture) {
  fixture.writeConfig();
  const result = fixture.run('osrm-datastore', ['--config', fixture.configPath]);
  assert.equal(result.status, 0, result.stderr);
  return result;
}

Then('a shared-memory deployment should serve with its release directory hidden', { timeout: 60000 }, async function () {
  const fixture = this.deployment;
  publish(fixture);
  fs.renameSync(fixture.output, `${fixture.output} hidden`);
  try {
    const server = await fixture.start();
    for (const profile of fixture.config.profiles) {
      const response = await fixture.request(server, `/route/v1/${profile.name}/7,43;7.012,43`);
      assert.equal(response.status, 200, response.bytes.toString());
    }
  } finally {
    fs.renameSync(`${fixture.output} hidden`, fixture.output);
  }
});

Given('the deployment is rebuilt with an open pedestrian area', { timeout: 60000 }, function () {
  const fixture = this.deployment;
  const input = path.join(fixture.directory, 'area.osm');
  const source = fs.readFileSync(fixture.config.input, 'utf8').replace('</osm>', `
    <node id="10000" lat="43.04" lon="7.04"/><node id="10001" lat="43.04" lon="7.042"/>
    <node id="10002" lat="43.042" lon="7.042"/><node id="10003" lat="43.042" lon="7.04"/>
    <way id="10000"><nd ref="10000"/><nd ref="10001"/><nd ref="10002"/><nd ref="10003"/><nd ref="10000"/>
      <tag k="highway" v="pedestrian"/><tag k="area" v="yes"/></way>
    <way id="10001"><nd ref="256"/><nd ref="10000"/><tag k="highway" v="footway"/></way>
    <way id="10002"><nd ref="255"/><nd ref="10002"/><tag k="highway" v="footway"/></way>
    </osm>`);
  fs.writeFileSync(input, source);
  fixture.config.input = input;
  fixture.config.profiles[1].lua = path.resolve('profiles/foot_area.lua');
  fixture.config.output_directory = 'area release';
  fixture.output = path.join(fixture.directory, 'area release');
  const result = fixture.prepare();
  assert.equal(result.status, 0, result.stderr);
  assert.ok(fs.existsSync(path.join(fixture.output, 'foot/map.osrm.openareas.fileIndex')));
});

Then('the shared-memory pedestrian area should be routable', async function () {
  const fixture = this.deployment;
  fs.renameSync(fixture.output, `${fixture.output} hidden`);
  try {
    const response = await fixture.request(fixture.processes.at(-1), '/route/v1/foot/7.0405,43.0405;7.0415,43.0415?radiuses=50;50');
    assert.equal(response.status, 200, response.bytes.toString());
  } finally {
    fs.renameSync(`${fixture.output} hidden`, fixture.output);
  }
});

Then('a shared-memory release refresh should update the selected profile', { timeout: 120000 }, async function () {
  const fixture = this.deployment;
  publish(fixture);
  const server = await fixture.start();
  const route = profile => `/route/v1/${profile}/7,43;7.012,43`;
  const oldCar = await fixture.request(server, route('car'));
  const oldFoot = await fixture.request(server, route('foot'));
  fixture.output = path.join(fixture.directory, 'release two');
  fixture.config.output_directory = 'release two';
  fixture.config.profiles[0].lua = path.resolve('profiles/foot.lua');
  const prepared = fixture.prepare();
  assert.equal(prepared.status, 0, prepared.stderr);
  publish(fixture);
  const deadline = Date.now() + 10000;
  let newCar;
  do {
    newCar = await fixture.request(server, route('car'));
    if (!newCar.bytes.equals(oldCar.bytes)) break;
    await new Promise(resolve => setTimeout(resolve, 10));
  } while (Date.now() < deadline);
  assert.equal(newCar.status, 200);
  assert.notDeepEqual(newCar, oldCar);
  assert.deepEqual(await fixture.request(server, route('foot')), oldFoot);
  assert.match(server.logs, new RegExp(`${fixture.namespace}/car`));
});

Then('a failed second publication should leave the first profile usable', { timeout: 60000 }, async function () {
  const fixture = this.deployment;
  fs.writeFileSync(path.join(fixture.output, 'foot/map.osrm.properties'), 'corrupt static archive');
  const result = fixture.run('osrm-datastore', ['--config', fixture.configPath]);
  assert.notEqual(result.status, 0);
  assert.match(result.stdout + result.stderr, /Published profile car/);
  assert.match(result.stderr, /Profile foot/);
  const standalone = await fixture.start(['--shared-memory', '--dataset-name', `${fixture.namespace}/car`, '-a', 'CH', '--port', '0', '--threads', '1']);
  assert.equal((await fixture.request(standalone, '/route/v1/car/7,43;7.012,43')).status, 200);
});

Then('configured datastore should reject legacy options and file storage', function () {
  const fixture = this.deployment;
  for (const flags of [['x.osrm'], ['--dataset-name', 'x'], ['--only-metric'], ['--list'], ['--list-blocks'], ['--remove-locks'], ['--spring-clean']]) {
    const result = fixture.run('osrm-datastore', ['--config', fixture.configPath, ...flags]);
    assert.notEqual(result.status, 0);
    assert.match(result.stderr, /conflict/);
  }
  fixture.config.server.storage = 'mmap';
  fixture.writeConfig();
  const result = fixture.run('osrm-datastore', ['--config', fixture.configPath]);
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /shared_memory/);
});
