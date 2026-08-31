// api/packages.js — GET /api/packages, POST /api/packages
// Source format: pip-wheel-inspired file-map {"rel/path":"content",...}
const { sql } = require('./_db');

function cors(res) {
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET,POST,OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type,Authorization');
}
function row2meta(r) {
  return {
    name: r.name, owner: r.owner, description: r.description || '',
    latest: r.latest, license: r.license || 'MIT',
    homepage: r.homepage, repository: r.repository,
    keywords: r.keywords || [], total_downloads: Number(r.total_downloads) || 0,
    created_at: r.created_at, updated_at: r.updated_at,
  };
}

module.exports = async function handler(req, res) {
  cors(res);
  if (req.method === 'OPTIONS') return res.status(200).end();

  if (req.method === 'GET') {
    const q     = (req.query.q || '').trim();
    const page  = Math.max(1, parseInt(req.query.page  || '1'));
    const limit = Math.min(100, parseInt(req.query.limit || '20'));
    let rows;
    if (q) {
      const like = `%${q}%`;
      rows = await sql`
        SELECT * FROM packages
        WHERE LOWER(name) LIKE LOWER(${like})
           OR LOWER(description) LIKE LOWER(${like})
           OR ${q} ILIKE ANY(keywords)
        ORDER BY total_downloads DESC`;
    } else {
      rows = await sql`SELECT * FROM packages ORDER BY updated_at DESC`;
    }
    const total = rows.length;
    const paged = rows.slice((page - 1) * limit, page * limit);
    return res.status(200).json({ total, page, limit, packages: paged.map(row2meta) });
  }

  if (req.method === 'POST') {
    const auth = req.headers.authorization;
    if (!auth?.startsWith('Bearer ')) return res.status(401).json({ error: 'Unauthorized' });
    const token = auth.slice(7);
    let username = null;
    const trows = await sql`SELECT username FROM api_tokens WHERE token = ${token} LIMIT 1`;
    if (trows.length) {
      username = trows[0].username;
    } else {
      try {
        const { jwtVerify } = await import('jose');
        const { getJwtSecret } = require('./_auth');
        const secret = getJwtSecret(); /* fail secure (finding #10) */
        const { payload } = await jwtVerify(Buffer.from(token, 'utf8'), secret);
        username = payload.username;
      } catch {
        return res.status(401).json({ error: 'Invalid token' });
      }
    }
    const { name, version, description, license, homepage, repository,
            keywords, dependencies, readme, changelog, source } = req.body;
    if (!name || !/^[a-z][a-z0-9_-]{0,63}$/.test(name))
      return res.status(400).json({ error: 'Invalid package name' });
    if (!version || !/^\d+\.\d+(\.\d+)?/.test(version))
      return res.status(400).json({ error: 'Invalid version format' });
    if (!description || description.trim().length < 5)
      return res.status(400).json({ error: 'description is required' });
    // Normalize source: wrap old plain-text in file-map format
    let sourceStr = source || '';
    if (sourceStr && !sourceStr.trim().startsWith('{')) {
      sourceStr = JSON.stringify({ [`${name}.ok`]: sourceStr });
    }
    const existing = await sql`SELECT owner FROM packages WHERE name = ${name} LIMIT 1`;
    if (existing.length && existing[0].owner !== username)
      return res.status(403).json({ error: `Package '${name}' is owned by @${existing[0].owner}` });
    const conflict = await sql`SELECT 1 FROM package_versions WHERE name=${name} AND version=${version} LIMIT 1`;
    if (conflict.length)
      return res.status(409).json({ error: `${name}@${version} already exists. Bump your version.` });
    const now = new Date().toISOString();
    const kw  = Array.isArray(keywords) ? keywords : [];
    const dep = JSON.stringify(dependencies || {});
    await sql`
      INSERT INTO packages (name,owner,description,latest,license,homepage,repository,keywords,total_downloads,created_at,updated_at)
      VALUES (${name},${username},${description},${version},${license||'MIT'},${homepage||null},${repository||null},${kw},0,${now},${now})
      ON CONFLICT (name) DO UPDATE SET description=EXCLUDED.description,latest=EXCLUDED.latest,
        license=EXCLUDED.license,homepage=EXCLUDED.homepage,repository=EXCLUDED.repository,
        keywords=EXCLUDED.keywords,updated_at=EXCLUDED.updated_at`;
    await sql`
      INSERT INTO package_versions (name,version,description,author,license,keywords,dependencies,readme,changelog,source,published_at,published_by)
      VALUES (${name},${version},${description},${username},${license||'MIT'},${kw},${dep},${readme||''},${changelog||''},${sourceStr},${now},${username})
      ON CONFLICT (name,version) DO NOTHING`;
    await sql`UPDATE users SET packages=array_append(packages,${name}) WHERE username=${username} AND NOT (packages @> ARRAY[${name}])`;
    let fileCount = 0;
    try { fileCount = Object.keys(JSON.parse(sourceStr)).length; } catch(_) { fileCount = 1; }
    return res.status(201).json({ success: true, message: `Published ${name}@${version} (${fileCount} file${fileCount===1?'':'s'})` });
  }

  return res.status(405).json({ error: 'Method not allowed' });
};
