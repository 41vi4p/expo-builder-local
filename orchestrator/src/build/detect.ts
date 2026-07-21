import fs from 'node:fs';
import path from 'node:path';
import { config } from '../config';
import type { ExpoProjectInfo } from '../types';

/** True if `targetPath` is inside (or equal to) one of the configured ALLOWED_ROOTS.
 * The directory browser and the build-start endpoint both gate on this so the tool
 * can never be pointed at an arbitrary host path outside what the operator opted in. */
export function isPathAllowed(targetPath: string): boolean {
  const resolved = path.resolve(targetPath);
  return config.allowedRoots.some((root) => resolved === root || resolved.startsWith(root + path.sep));
}

function readJsonSafe(filePath: string): any | null {
  try {
    return JSON.parse(fs.readFileSync(filePath, 'utf8'));
  } catch {
    return null;
  }
}

/** Inspects a directory to decide whether it's a buildable managed-Expo project root,
 * and if so, surfaces the metadata the GUI shows in the picker (name, version,
 * android package, declared profiles) without needing to run any tooling. */
export function detectExpoProject(dirPath: string): ExpoProjectInfo {
  const packageJsonPath = path.join(dirPath, 'package.json');
  if (!fs.existsSync(packageJsonPath)) {
    return { isExpoProject: false, reason: 'No package.json found in this directory' };
  }
  const pkg = readJsonSafe(packageJsonPath);
  if (!pkg) {
    return { isExpoProject: false, reason: 'package.json exists but could not be parsed' };
  }
  const hasExpoDep = Boolean(pkg.dependencies?.expo || pkg.devDependencies?.expo);
  if (!hasExpoDep) {
    return { isExpoProject: false, reason: "package.json has no 'expo' dependency" };
  }

  const appJson = readJsonSafe(path.join(dirPath, 'app.json'))?.expo;
  const androidPackage: string | undefined = appJson?.android?.package;
  const androidVersionCode: number | undefined = appJson?.android?.versionCode;

  const easJson = readJsonSafe(path.join(dirPath, 'eas.json'));
  const easProfiles = easJson?.build ? Object.keys(easJson.build) : undefined;

  return {
    isExpoProject: true,
    name: pkg.name,
    version: pkg.version,
    androidPackage,
    androidVersionCode,
    easProfiles,
    hasGoogleServicesJson: fs.existsSync(path.join(dirPath, 'google-services.json')),
    hasEnvFile: fs.existsSync(path.join(dirPath, '.env')),
  };
}
