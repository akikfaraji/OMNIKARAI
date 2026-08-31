// api/user/tokens.js — GET / POST / DELETE /api/user/tokens
const { sql } = require('../_db');
const { jwtVerify } = require('jose');

const { getJwtSecret } = require('../_auth');
// Fail secure (finding #10): no hardcoded fallback — throws when JWT_SECRET is unset.
const SECRET = getJwtSecret();

function cors(res) {
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET,POST,DELETE,OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type,Authorization');
}

async function authUser(req) {
  const auth = req.headers.authorization;
  let tok = auth?.startsWith('Bearer ') ? auth.slice(7) : null;
  if (!tok) { const m = (req.headers.cookie||'').match(/opi_token=([^;]+)/); tok = m?m[1]:null; }
  if (!tok) return null;
  try {
    const { payload } = await jwtVerify(Buffer.from(tok), SECRET);
    const r = await sql`SELECT username FROM users WHERE username=${payload.username} LIMIT 1`;
    return r[0]?.username || null;
  } catch { return null; }
}

function genToken() {
  const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
  let t = 'opi_';
  for (let i = 0; i < 40; i++) t += chars[Math.floor(Math.random() * chars.length)];
  return t;
}

module.exports = async function handler(req, res) {
  cors(res);
  if (req.method === 'OPTIONS') return res.status(200).end();
  const username = await authUser(req);
  if (!username) return res.status(401).json({ error: 'Unauthorized' });

  if (req.method === 'GET') {
    const rows = await sql`SELECT token, label, created_at FROM api_tokens WHERE username=${username} ORDER BY created_at DESC`;
    return res.status(200).json({
      tokens: rows.map(t => ({
        display: t.token.slice(0,10) + '...' + t.token.slice(-4),
        label: t.label, created_at: t.created_at
      }))
    });
  }

  if (req.method === 'POST') {
    const count = await sql`SELECT COUNT(*) as c FROM api_tokens WHERE username=${username}`;
    if (Number(count[0].c) >= 10) return res.status(400).json({ error: 'Maximum 10 tokens per account' });
    const token = genToken();
    const label = req.body?.label || 'default';
    await sql`INSERT INTO api_tokens (token, username, label) VALUES (${token}, ${username}, ${label})`;
    return res.status(201).json({ token, message: 'Save this token — it will not be shown again' });
  }

  if (req.method === 'DELETE') {
    const prefix = req.query.token;
    if (!prefix) return res.status(400).json({ error: 'token param required' });
    const rows = await sql`SELECT token FROM api_tokens WHERE username=${username}`;
    const match = rows.find(r => r.token.startsWith(prefix.replace(/\.\.\..+$/, '')));
    if (!match) return res.status(404).json({ error: 'Token not found' });
    await sql`DELETE FROM api_tokens WHERE token=${match.token}`;
    return res.status(200).json({ ok: true });
  }

  return res.status(405).json({ error: 'Method not allowed' });
};
