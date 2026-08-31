// api/user/profile.js — PATCH /api/user/profile
// api/user/[username].js — GET /api/user/:username
const { sql } = require('../_db');
const { jwtVerify } = require('jose');

const { getJwtSecret } = require('../_auth');
// Fail secure (finding #10): no hardcoded fallback — throws when JWT_SECRET is unset.
const SECRET = getJwtSecret();

function cors(res) {
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET,PATCH,OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type,Authorization');
}

async function authUser(req) {
  const auth = req.headers.authorization;
  let tok = auth?.startsWith('Bearer ') ? auth.slice(7) : null;
  if (!tok) { const m = (req.headers.cookie||'').match(/opi_token=([^;]+)/); tok = m?m[1]:null; }
  if (!tok) return null;
  try {
    const { payload } = await jwtVerify(Buffer.from(tok), SECRET);
    return payload.username;
  } catch { return null; }
}

module.exports = async function handler(req, res) {
  cors(res);
  if (req.method === 'OPTIONS') return res.status(200).end();

  // PATCH /api/user/profile — update own profile
  if (req.method === 'PATCH') {
    const username = await authUser(req);
    if (!username) return res.status(401).json({ error: 'Unauthorized' });
    const { display_name, bio, website } = req.body || {};
    await sql`UPDATE users SET
      display_name = COALESCE(${display_name?.trim() || null}, display_name),
      bio          = COALESCE(${bio?.trim()          ?? null}, bio),
      website      = COALESCE(${website?.trim()      ?? null}, website)
      WHERE username = ${username}`;
    return res.status(200).json({ ok: true });
  }

  // GET /api/user/:username — public profile + their packages
  if (req.method === 'GET') {
    const uname = req.query.username || req.url.split('/').filter(Boolean).pop()?.split('?')[0];
    if (!uname) return res.status(400).json({ error: 'username required' });
    const rows = await sql`SELECT username, display_name, bio, website, joined_at, packages FROM users WHERE username=${uname.toLowerCase()} LIMIT 1`;
    if (!rows.length) return res.status(404).json({ error: 'User not found' });
    const user = rows[0];
    const pkgNames = user.packages || [];
    const pkgs = [];
    for (const name of pkgNames) {
      const pr = await sql`SELECT name, latest, description, total_downloads, updated_at FROM packages WHERE name=${name} LIMIT 1`;
      if (pr.length) pkgs.push(pr[0]);
    }
    return res.status(200).json({ ...user, packages: pkgs });
  }

  return res.status(405).json({ error: 'Method not allowed' });
};
