// api/auth/login.js — POST /api/auth/login
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

  const { username, password } = req.body || {};
  if (!username || !password)
    return res.status(400).json({ error: 'username and password required' });

  try {
    const isEmail = username.includes('@');
    const rows = isEmail
      ? await sql`SELECT * FROM users WHERE email=${username.toLowerCase()} LIMIT 1`
      : await sql`SELECT * FROM users WHERE username=${username.toLowerCase()} LIMIT 1`;

    if (!rows.length) return res.status(401).json({ error: 'Invalid username or password' });
    const user = rows[0];

    const ok = await bcrypt.compare(password, user.password);
    if (!ok) return res.status(401).json({ error: 'Invalid username or password' });

    const token = await new SignJWT({ username: user.username })
      .setProtectedHeader({ alg: 'HS256' }).setIssuedAt().setExpirationTime('30d').sign(SECRET);

    res.setHeader('Set-Cookie', `opi_token=${token}; Path=/; Max-Age=2592000; HttpOnly; Secure; SameSite=Lax`);
    return res.status(200).json({
      token,
      user: { username: user.username, email: user.email, display_name: user.display_name }
    });
  } catch (e) {
    return res.status(500).json({ error: e.message });
  }
};
