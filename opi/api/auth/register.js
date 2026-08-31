// api/auth/register.js — POST /api/auth/register
const { sql } = require('../_db');
const bcrypt   = require('bcryptjs');
const { SignJWT } = require('jose');

const { getJwtSecret } = require('../_auth');
// Fail secure (finding #10): no hardcoded fallback — throws when JWT_SECRET is unset.
const SECRET = getJwtSecret();

function cors(res) {
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'POST,OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type');
}

module.exports = async function handler(req, res) {
  cors(res);
  if (req.method === 'OPTIONS') return res.status(200).end();
  if (req.method !== 'POST') return res.status(405).json({ error: 'Method not allowed' });

  const { username, email, password, display_name } = req.body || {};
  if (!username || !email || !password)
    return res.status(400).json({ error: 'username, email and password required' });
  if (!/^[a-zA-Z][a-zA-Z0-9_-]{2,31}$/.test(username))
    return res.status(400).json({ error: 'Username: 3-32 chars, start with a letter' });
  if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email))
    return res.status(400).json({ error: 'Invalid email address' });
  if (password.length < 8)
    return res.status(400).json({ error: 'Password must be at least 8 characters' });

  const uname = username.toLowerCase();
  try {
    const existing = await sql`SELECT 1 FROM users WHERE username=${uname} OR email=${email.toLowerCase()} LIMIT 1`;
    if (existing.length) return res.status(409).json({ error: 'Username or email already taken' });

    const hash = await bcrypt.hash(password, 12);
    await sql`INSERT INTO users (username, email, password, display_name, bio, website, joined_at, is_verified, packages)
              VALUES (${uname}, ${email.toLowerCase()}, ${hash}, ${display_name || uname}, '', '', NOW(), false, '{}')`;

    const token = await new SignJWT({ username: uname })
      .setProtectedHeader({ alg: 'HS256' }).setIssuedAt().setExpirationTime('30d').sign(SECRET);

    res.setHeader('Set-Cookie', `opi_token=${token}; Path=/; Max-Age=2592000; HttpOnly; Secure; SameSite=Lax`);
    return res.status(201).json({ token, user: { username: uname, email: email.toLowerCase(), display_name: display_name || uname } });
  } catch (e) {
    return res.status(500).json({ error: e.message });
  }
};
