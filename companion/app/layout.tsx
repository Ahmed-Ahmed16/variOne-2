import type { Metadata, Viewport } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "VariOne Companion · Vemo",
  description:
    "Gamified companion for the VariOne awareness device — meet Vemo, level up, and track your operations.",
};

export const viewport: Viewport = {
  themeColor: "#070b10",
  width: "device-width",
  initialScale: 1,
};

export default function RootLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}
