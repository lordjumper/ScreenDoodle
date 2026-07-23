'use strict';

const https = require('https');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const OWNER = 'lordjumper';
const REPO = 'ScreenDoodle';
const API = 'api.github.com';
const UA = 'ScreenDoodle-Launcher';

function getJson(pathname) {
  return new Promise((resolve, reject) => {
    const req = https.request(
      {
        host: API,
        path: pathname,
        method: 'GET',
        headers: {
          'User-Agent': UA,
          Accept: 'application/vnd.github+json'
        },
        timeout: 15000
      },
      (res) => {
        if (res.statusCode === 403 && res.headers['x-ratelimit-remaining'] === '0') {
          res.resume();
          return reject(new Error('GitHub rate limit reached. Try again in a few minutes.'));
        }
        if (res.statusCode < 200 || res.statusCode >= 300) {
          res.resume();
          return reject(new Error(`GitHub returned HTTP ${res.statusCode}`));
        }
        let body = '';
        res.setEncoding('utf8');
        res.on('data', (c) => (body += c));
        res.on('end', () => {
          try {
            resolve(JSON.parse(body));
          } catch (e) {
            reject(new Error('Malformed response from GitHub'));
          }
        });
      }
    );
    req.on('timeout', () => req.destroy(new Error('GitHub request timed out')));
    req.on('error', reject);
    req.end();
  });
}

function shapeRelease(r) {
  return {
    tag: r.tag_name,
    version: stripV(r.tag_name),
    name: r.name || r.tag_name,
    body: r.body || '',
    url: r.html_url,
    publishedAt: r.published_at,
    prerelease: !!r.prerelease,
    assets: (r.assets || []).map((a) => ({
      name: a.name,
      size: a.size,
      url: a.browser_download_url,
      downloadCount: a.download_count
    }))
  };
}

async function latestRelease() {
  return shapeRelease(await getJson(`/repos/${OWNER}/${REPO}/releases/latest`));
}

async function listReleases(limit = 12) {
  const rs = await getJson(`/repos/${OWNER}/${REPO}/releases?per_page=${limit}`);
  return rs.map(shapeRelease);
}

function stripV(tag) {
  return String(tag || '').replace(/^[vV]/, '');
}

function compareVersions(a, b) {
  const pa = stripV(a).split(/[.\-+]/);
  const pb = stripV(b).split(/[.\-+]/);
  for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
    const na = parseInt(pa[i], 10);
    const nb = parseInt(pb[i], 10);
    const va = Number.isNaN(na) ? 0 : na;
    const vb = Number.isNaN(nb) ? 0 : nb;
    if (va !== vb) return va < vb ? -1 : 1;
  }
  return 0;
}

function pickAsset(release) {
  const assets = release.assets || [];
  const portable = assets.find((a) => /^ScreenDoodle[-_]?[\d.]*\.exe$/i.test(a.name));
  if (portable) return { ...portable, kind: 'portable' };

  const zip = assets.find((a) => /\.zip$/i.test(a.name));
  if (zip) return { ...zip, kind: 'zip' };

  const setup = assets.find(
    (a) => /\.exe$/i.test(a.name) && /(setup|install)/i.test(a.name)
  );
  if (setup) return { ...setup, kind: 'setup' };

  const anyExe = assets.find((a) => /\.exe$/i.test(a.name));
  if (anyExe) return { ...anyExe, kind: 'setup' };

  return null;
}

function download(url, destFile, onProgress, redirects = 0) {
  return new Promise((resolve, reject) => {
    if (redirects > 5) return reject(new Error('Too many redirects'));

    fs.mkdirSync(path.dirname(destFile), { recursive: true });

    const req = https.get(
      url,
      { headers: { 'User-Agent': UA, Accept: 'application/octet-stream' }, timeout: 30000 },
      (res) => {
        if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
          res.resume();
          return resolve(download(res.headers.location, destFile, onProgress, redirects + 1));
        }
        if (res.statusCode !== 200) {
          res.resume();
          return reject(new Error(`Download failed: HTTP ${res.statusCode}`));
        }

        const total = parseInt(res.headers['content-length'] || '0', 10);
        let received = 0;
        const hash = crypto.createHash('sha256');
        const out = fs.createWriteStream(destFile);

        res.on('data', (chunk) => {
          received += chunk.length;
          hash.update(chunk);
          if (onProgress) onProgress(received, total);
        });
        res.pipe(out);

        out.on('error', (e) => {
          try { fs.unlinkSync(destFile); } catch (_) {}
          reject(e);
        });
        out.on('finish', () =>
          resolve({ path: destFile, bytes: received, sha256: hash.digest('hex') })
        );
      }
    );
    req.on('timeout', () => req.destroy(new Error('Download timed out')));
    req.on('error', reject);
  });
}

function sha256File(file) {
  return new Promise((resolve, reject) => {
    const hash = crypto.createHash('sha256');
    const s = fs.createReadStream(file);
    s.on('error', reject);
    s.on('data', (c) => hash.update(c));
    s.on('end', () => resolve(hash.digest('hex')));
  });
}

module.exports = {
  OWNER,
  REPO,
  latestRelease,
  listReleases,
  compareVersions,
  stripV,
  pickAsset,
  download,
  sha256File,
  releasesPage: `https://github.com/${OWNER}/${REPO}/releases`
};
