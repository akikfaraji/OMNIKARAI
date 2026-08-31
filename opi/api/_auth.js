// opi/_auth.js — shared fail-secure JWT secret resolution (finding #10 fix)
//
// The registry previously fell back to the hardcoded 'opi-dev-secret' when
// JWT_SECRET was unset, which let ANYONE mint valid tokens on a deployment
// that forgot the env var. Fail secure instead: without an explicitly
// configured secret, auth endpoints return 503 and token verification
// throws — a misconfigured deployment refuses to authenticate rather than
// silently trusting a public default.
function getJwtSecret() {
  const secret = process.env.JWT_SECRET;
  if (!secret || secret.length < 16) {
    throw new Error('JWT_SECRET is not configured (must be >= 16 chars)');
  }
  return new TextEncoder().encode(secret);
}

module.exports = { getJwtSecret };
