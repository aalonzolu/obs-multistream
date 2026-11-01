import { hmacSha256Hex } from "./crypto";
import type { Env } from "./env";

export interface GraphError {
  code: number;
  subcode?: number;
  type?: string;
  message: string;
}

export interface GraphResult<T> {
  ok: boolean;
  status: number;
  data: T;
  error?: GraphError;
}

export interface TokenInfo {
  app_id?: string;
  is_valid?: boolean;
  scopes?: string[];
  profile_id?: string;
  granular_scopes?: Array<{ scope: string; target_ids?: string[] }>;
}

export class Graph {
  private readonly base: string;

  constructor(private readonly env: Env) {
    this.base = `https://graph.facebook.com/${env.META_GRAPH_VERSION}`;
  }

  get version(): string {
    return this.env.META_GRAPH_VERSION;
  }

  async call<T>(method: "GET" | "POST" | "DELETE", path: string, token: string, params: Record<string, string> = {}): Promise<GraphResult<T>> {
    const query = new URLSearchParams({ ...params, access_token: token, appsecret_proof: await hmacSha256Hex(this.env.META_APP_SECRET, token) });
    const init: RequestInit = { method };
    let url = `${this.base}${path}`;
    if (method !== "POST") {
      url += `?${query}`;
    } else {
      init.body = query;
      init.headers = { "Content-Type": "application/x-www-form-urlencoded" };
    }
    const response = await fetch(url, init);
    const data = (await response.json().catch(() => ({}))) as T & { error?: { code: number; error_subcode?: number; type?: string; message: string } };
    const error = data?.error ? { code: data.error.code, subcode: data.error.error_subcode, type: data.error.type, message: data.error.message } : undefined;
    return { ok: response.ok && !error, status: response.status, data, error };
  }

  async exchangeCode(code: string, redirectUri: string): Promise<GraphResult<{ access_token?: string }>> {
    const query = new URLSearchParams({
      client_id: this.env.META_APP_ID,
      client_secret: this.env.META_APP_SECRET,
      redirect_uri: redirectUri,
      code,
    });
    const response = await fetch(`${this.base}/oauth/access_token?${query}`);
    const data = (await response.json().catch(() => ({}))) as { access_token?: string; error?: GraphError };
    return { ok: response.ok && !!data.access_token, status: response.status, data, error: data.error };
  }

  async longLived(token: string): Promise<GraphResult<{ access_token?: string }>> {
    const query = new URLSearchParams({
      grant_type: "fb_exchange_token",
      client_id: this.env.META_APP_ID,
      client_secret: this.env.META_APP_SECRET,
      fb_exchange_token: token,
    });
    const response = await fetch(`${this.base}/oauth/access_token?${query}`);
    const data = (await response.json().catch(() => ({}))) as { access_token?: string; error?: GraphError };
    return { ok: response.ok && !!data.access_token, status: response.status, data, error: data.error };
  }

  async debugToken(token: string): Promise<GraphResult<{ data?: TokenInfo }>> {
    const query = new URLSearchParams({ input_token: token, access_token: `${this.env.META_APP_ID}|${this.env.META_APP_SECRET}` });
    const response = await fetch(`${this.base}/debug_token?${query}`);
    const data = (await response.json().catch(() => ({}))) as { data?: TokenInfo };
    return { ok: response.ok, status: response.status, data };
  }
}
