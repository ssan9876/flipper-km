export function toBase64(text) {
  const bytes = new TextEncoder().encode(text);
  let binary = '';
  for (const b of bytes) binary += String.fromCharCode(b);
  return btoa(binary);
}

export function buildCommand(text) {
  return `kmtype ${toBase64(text)}\r`;
}

export function chunk(bytes, size) {
  const parts = [];
  for (let i = 0; i < bytes.length; i += size) {
    parts.push(bytes.slice(i, i + size));
  }
  return parts;
}
