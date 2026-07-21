#!/usr/bin/env node
/**
 * Injects a `release` signingConfig into a freshly `expo prebuild`-generated
 * android/app/build.gradle, backed by a gitignore-friendly keystore.properties file,
 * so `./gradlew assembleRelease` / `bundleRelease` produces a properly-signed artifact
 * instead of Expo's default debug-signed release build.
 *
 * This follows the same convention the official React Native docs recommend for local
 * (non-EAS) release signing: https://reactnative.dev/docs/signed-apk-android
 *
 * Best-effort: Expo's generated build.gradle is a template, not a stable API, so this
 * uses a small brace-depth scanner rather than a full Groovy parser. If the expected
 * `signingConfigs { debug { ... } }` / `buildTypes { release { ... } }` shapes aren't
 * found, it fails loudly with a clear message instead of silently producing an
 * unsigned or wrongly-signed build.
 *
 * All secrets passed on the CLI are written only to files inside the (ephemeral,
 * per-build) android/ directory; build-entrypoint.sh deletes them in its EXIT trap.
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

/** Finds the `{ ... }` block belonging to the first match of `header` after `fromIndex`.
 *  Returns { blockStart, blockEnd, openBrace, closeBrace } (indices into `text`), or null. */
function findBlock(text, header, fromIndex = 0) {
  const headerIdx = text.indexOf(header, fromIndex);
  if (headerIdx === -1) return null;
  const openBrace = text.indexOf('{', headerIdx);
  if (openBrace === -1) return null;
  let depth = 1;
  let i = openBrace + 1;
  for (; i < text.length && depth > 0; i++) {
    if (text[i] === '{') depth++;
    else if (text[i] === '}') depth--;
  }
  if (depth !== 0) return null;
  const closeBrace = i - 1;
  return { headerIdx, openBrace, closeBrace };
}

function main() {
  const args = parseArgs(process.argv.slice(2));
  const androidDir = args.androidDir;
  const keystoreSrc = args.keystore;
  const storePassword = args.storePassword || '';
  const keyAlias = args.keyAlias || '';
  const keyPassword = args.keyPassword || storePassword;

  if (!androidDir || !fs.existsSync(androidDir)) {
    console.error(`android dir not found: ${androidDir}`);
    process.exit(2);
  }
  if (!keystoreSrc || !fs.existsSync(keystoreSrc)) {
    console.error(`keystore file not found: ${keystoreSrc}`);
    process.exit(2);
  }
  if (!storePassword || !keyAlias) {
    console.error('storePassword and keyAlias are required for release signing');
    process.exit(2);
  }

  const buildGradlePath = path.join(androidDir, 'app', 'build.gradle');
  if (!fs.existsSync(buildGradlePath)) {
    console.error(`build.gradle not found at ${buildGradlePath} — did expo prebuild run?`);
    process.exit(2);
  }

  // 1. Copy the keystore into the (ephemeral) android/app directory.
  const keystoreDest = path.join(androidDir, 'app', 'release.keystore');
  fs.copyFileSync(keystoreSrc, keystoreDest);

  // 2. Write keystore.properties at the android/ (rootProject) level, absolute store path
  //    so Gradle's file() resolves it regardless of working directory.
  const propsPath = path.join(androidDir, 'keystore.properties');
  const props = [
    `storeFile=${keystoreDest.replace(/\\/g, '\\\\')}`,
    `storePassword=${storePassword}`,
    `keyAlias=${keyAlias}`,
    `keyPassword=${keyPassword}`,
    '',
  ].join('\n');
  fs.writeFileSync(propsPath, props, { mode: 0o600 });

  // 3. Patch build.gradle.
  let gradle = fs.readFileSync(buildGradlePath, 'utf8');

  if (!gradle.includes('keystorePropertiesFile')) {
    const header = [
      'def keystorePropertiesFile = rootProject.file("keystore.properties")',
      'def keystoreProperties = new Properties()',
      'if (keystorePropertiesFile.exists()) {',
      '    keystoreProperties.load(new FileInputStream(keystorePropertiesFile))',
      '}',
      '',
    ].join('\n');
    // Insert right after the first `apply plugin` line (present in every Expo/RN
    // generated app/build.gradle), which keeps the insertion point stable across
    // template variations (new-arch on/off, Kotlin plugin present or not).
    const applyMatch = gradle.match(/^apply plugin:.*$/m);
    if (!applyMatch) {
      console.error('Could not find an `apply plugin:` line in app/build.gradle to anchor the patch');
      process.exit(3);
    }
    const insertAt = applyMatch.index + applyMatch[0].length;
    gradle = gradle.slice(0, insertAt) + '\n\n' + header + gradle.slice(insertAt);
  }

  const signingConfigsBlock = findBlock(gradle, 'signingConfigs {');
  if (!signingConfigsBlock) {
    console.error('Could not find `signingConfigs { ... }` block in app/build.gradle');
    process.exit(3);
  }
  if (!gradle.slice(signingConfigsBlock.openBrace, signingConfigsBlock.closeBrace).includes('release {')) {
    const releaseConfig = [
      '',
      '        release {',
      '            if (keystorePropertiesFile.exists()) {',
      "                storeFile file(keystoreProperties['storeFile'])",
      "                storePassword keystoreProperties['storePassword']",
      "                keyAlias keystoreProperties['keyAlias']",
      "                keyPassword keystoreProperties['keyPassword']",
      '            }',
      '        }',
    ].join('\n');
    gradle =
      gradle.slice(0, signingConfigsBlock.closeBrace) +
      releaseConfig +
      '\n    ' +
      gradle.slice(signingConfigsBlock.closeBrace);
  }

  // Re-locate buildTypes after the edit above (indices shifted).
  const buildTypesBlock = findBlock(gradle, 'buildTypes {');
  if (!buildTypesBlock) {
    console.error('Could not find `buildTypes { ... }` block in app/build.gradle');
    process.exit(3);
  }
  const releaseTypeBlock = findBlock(
    gradle,
    'release {',
    buildTypesBlock.openBrace
  );
  if (!releaseTypeBlock || releaseTypeBlock.headerIdx > buildTypesBlock.closeBrace) {
    console.error('Could not find `release { ... }` build type block in app/build.gradle');
    process.exit(3);
  }
  const releaseTypeSrc = gradle.slice(releaseTypeBlock.openBrace, releaseTypeBlock.closeBrace);
  if (/signingConfig\s+signingConfigs\.debug/.test(releaseTypeSrc)) {
    const patchedReleaseType = releaseTypeSrc.replace(
      /signingConfig\s+signingConfigs\.debug/,
      "signingConfig keystorePropertiesFile.exists() ? signingConfigs.release : signingConfigs.debug"
    );
    gradle =
      gradle.slice(0, releaseTypeBlock.openBrace) +
      patchedReleaseType +
      gradle.slice(releaseTypeBlock.closeBrace);
  } else if (!releaseTypeSrc.includes('signingConfigs.release')) {
    console.warn(
      'Warning: release buildType did not reference signingConfigs.debug as expected; ' +
      'leaving it untouched. The build may still use the debug keystore.'
    );
  }

  fs.writeFileSync(buildGradlePath, gradle, 'utf8');
  console.log('Release signing configured via keystore.properties');
}

main();
