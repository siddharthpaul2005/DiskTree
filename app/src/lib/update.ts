// Checks GitHub for a newer release. Silent on any failure (offline, rate limit).

export const RELEASES_API = "https://api.github.com/repos/siddharthpaul2005/DiskTree/releases/latest";

export interface UpdateInfo { version: string; url: string }

/** "v1.10.0" > "1.9.2" */
export function isNewer(candidate: string, current: string): boolean {
  const parse = (v: string) => v.replace(/^v/i, "").split(/[.-]/).slice(0, 3).map((x) => parseInt(x, 10) || 0);
  const a = parse(candidate), b = parse(current);
  for (let i = 0; i < 3; i++) {
    if ((a[i] ?? 0) !== (b[i] ?? 0)) return (a[i] ?? 0) > (b[i] ?? 0);
  }
  return false;
}

export async function checkForUpdate(current: string): Promise<UpdateInfo | null> {
  try {
    const res = await fetch(RELEASES_API, { headers: { Accept: "application/vnd.github+json" } });
    if (!res.ok) return null;
    const rel = await res.json();
    const tag: string = rel?.tag_name ?? "";
    if (!tag || rel?.draft || rel?.prerelease || !isNewer(tag, current)) return null;
    return { version: tag.replace(/^v/i, ""), url: rel.html_url };
  } catch {
    return null;
  }
}
