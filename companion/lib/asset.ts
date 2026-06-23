// Prefix a public asset path with the build-time base path so raw <img src> refs
// resolve correctly when the app is served from a subfolder (e.g. /companion).
// Next rewrites internal links + /_next chunks automatically, but NOT raw string
// asset paths, so we do it explicitly here. Empty base path = served at root.
const BASE = process.env.NEXT_PUBLIC_BASE_PATH || "";

export function asset(path: string): string {
  return BASE + path;
}
