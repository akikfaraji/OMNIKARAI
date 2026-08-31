// api/auth/me.js — GET /api/auth/me
const { sql } = require('../_db');
const { jwtVerify } = require('jose');

const { getJwtSecret } = require('../_auth');
// Fail secure (finding #10): no hardcoded fallback — throws when JWT_SECRET is unset.
const SECRET = getJwtSecret();

async function getUser(req) {
  const auth = req.headers.authorization;
  let token = auth?.startsWith('Bearer ') ? auth.slice(7) : null;
  if (!token) {
    const cookie = req.headers.cookie || '';
    const m = cookie.match(/opi_token=([^;]+)/);
    token = m ? m[1] : null;
  }
  if (!token) return null;
  try {
    const { payload } = await jwtVerify(Buffer.from(token), SECRET);
    const rows = await sql`SELECT * FROM users WHERE username=${payload.username} LIMIT 1`;
    return rows[0] || null;
  } catch { return null; }
}

module.exports = async function handler(req, res) {
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type,Authorization');
  if (req.method !== 'GET') return res.status(405).json({ error: 'Method not allowed' });
  const user = await getUser(req);
  if (!user) return res.status(401).json({ error: 'Unauthorized' });
  return res.status(200).json({
    username: user.username, email: user.email, display_name: user.display_name,
    bio: user.bio, website: user.website, joined_at: user.joined_at,
    packages: user.packages || [], is_verified: user.is_verified,
  });
};
