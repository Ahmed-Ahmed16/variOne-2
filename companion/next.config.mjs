/** @type {import('next').NextConfig} */
// Static export so the companion can be dropped into the static varione-site repo
// under a subpath (e.g. /companion). BASE_PATH is supplied at build time:
//   NEXT_PUBLIC_BASE_PATH=/companion npm run build   -> out/ served at /companion
// With it empty (default) the app builds/serves at the root (standalone dev).
const basePath = process.env.NEXT_PUBLIC_BASE_PATH || "";

const nextConfig = {
  reactStrictMode: true,
  output: "export",
  trailingSlash: true,
  basePath: basePath || undefined,
  assetPrefix: basePath || undefined,
  images: { unoptimized: true },
};

export default nextConfig;
