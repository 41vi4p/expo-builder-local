#!/usr/bin/env node
/**
 * Prepares a local, uploaded keystore for use with `eas build --local`.
 *
 * EAS normally manages Android signing credentials remotely. To use a keystore the
 * developer uploaded through the GUI instead, we:
 *   1. Write a `credentials.json` at the project root in the shape `eas build --local`
 *      expects for a local Android keystore.
 *   2. Temporarily set `build.<profile>.credentialsSource = "local"` in the project's
 *      eas.json so the CLI actually reads that file instead of calling the EAS API.
 *
 * Both files are written into the bind-mounted host project directory, so
 * build-entrypoint.sh backs up the original eas.json (restored in its EXIT trap) and
 * deletes credentials.json once the build finishes, success or fail — neither the
 * keystore password nor the override should persist on the developer's disk.
 */
'use strict';

const fs = require('fs');
const path = require('path');

function parseArgs(argv) {
  const out = {};
  for (let i = 0; i < argv.length; i += 2) {
    const key = argv[i].replace(/^--/, '');
    out[key] = argv[i + 1];
  }
  return out;
}

function main() {
  const args = parseArgs(process.argv.slice(2));
  const projectDir = args.projectDir;
  const profile = args.profile;
  const keystorePath = args.keystore;
  const storePassword = args.storePassword || '';
  const keyAlias = args.keyAlias || '';
  const keyPassword = args.keyPassword || storePassword;

  if (!projectDir || !fs.existsSync(projectDir)) {
    console.error(`projectDir not found: ${projectDir}`);
    process.exit(2);
  }
  if (!keystorePath || !fs.existsSync(keystorePath)) {
    console.error(`keystore file not found: ${keystorePath}`);
    process.exit(2);
  }
  if (!storePassword || !keyAlias) {
    console.error('storePassword and keyAlias are required for release signing');
    process.exit(2);
  }

  const credentials = {
    android: {
      keystore: {
        keystorePath: path.resolve(keystorePath),
        keystorePassword: storePassword,
        keyAlias,
        keyPassword,
      },
    },
  };
  fs.writeFileSync(
    path.join(projectDir, 'credentials.json'),
    JSON.stringify(credentials, null, 2),
    { mode: 0o600 }
  );

  const easJsonPath = path.join(projectDir, 'eas.json');
  const easJson = JSON.parse(fs.readFileSync(easJsonPath, 'utf8'));
  easJson.build = easJson.build || {};
  easJson.build[profile] = easJson.build[profile] || {};
  easJson.build[profile].credentialsSource = 'local';
  fs.writeFileSync(easJsonPath, JSON.stringify(easJson, null, 2) + '\n', 'utf8');

  console.log(`Local EAS credentials prepared for profile "${profile}"`);
}

main();
