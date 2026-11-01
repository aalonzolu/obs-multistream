const encoder = new TextEncoder();
const decoder = new TextDecoder();

export function base64Url(bytes: ArrayBuffer | Uint8Array): string {
  const view = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  let binary = "";
  for (const byte of view) binary += String.fromCharCode(byte);
  return btoa(binary).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}

function fromBase64Url(text: string): Uint8Array {
  const padded = text.replace(/-/g, "+").replace(/_/g, "/").padEnd(Math.ceil(text.length / 4) * 4, "=");
  return Uint8Array.from(atob(padded), (c) => c.charCodeAt(0));
}

export function randomToken(bytes = 32): string {
  const buffer = new Uint8Array(bytes);
  crypto.getRandomValues(buffer);
  return base64Url(buffer);
}

export async function sha256Base64Url(text: string): Promise<string> {
  return base64Url(await crypto.subtle.digest("SHA-256", encoder.encode(text)));
}

export function constantTimeEqual(a: string, b: string): boolean {
  if (a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return diff === 0;
}

export async function hmacSha256Hex(key: string, message: string): Promise<string> {
  const cryptoKey = await crypto.subtle.importKey("raw", encoder.encode(key), { name: "HMAC", hash: "SHA-256" }, false, ["sign"]);
  const signature = new Uint8Array(await crypto.subtle.sign("HMAC", cryptoKey, encoder.encode(message)));
  return [...signature].map((b) => b.toString(16).padStart(2, "0")).join("");
}

async function aesKey(secret: string): Promise<CryptoKey> {
  return crypto.subtle.importKey("raw", fromBase64Url(secret), "AES-GCM", false, ["encrypt", "decrypt"]);
}

export async function seal(secret: string, value: unknown): Promise<string> {
  const iv = new Uint8Array(12);
  crypto.getRandomValues(iv);
  const ciphertext = await crypto.subtle.encrypt({ name: "AES-GCM", iv }, await aesKey(secret), encoder.encode(JSON.stringify(value)));
  return `${base64Url(iv)}.${base64Url(ciphertext)}`;
}

export async function unseal<T>(secret: string, sealed: string): Promise<T> {
  const [iv, ciphertext] = sealed.split(".");
  const plain = await crypto.subtle.decrypt({ name: "AES-GCM", iv: fromBase64Url(iv) }, await aesKey(secret), fromBase64Url(ciphertext));
  return JSON.parse(decoder.decode(plain)) as T;
}
