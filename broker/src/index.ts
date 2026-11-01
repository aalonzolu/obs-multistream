import { constantTimeEqual, randomToken, seal, sha256Base64Url, unseal } from "./crypto";
import type { Env } from "./env";
import { Graph, type GraphError } from "./graph";
import { dataDeletionPage, homePage, privacyPage, resultPage, termsPage } from "./pages";

const TRANSACTION_TTL_MS = 5 * 60 * 1000;
const OPERATION_TTL_MS = 24 * 60 * 60 * 1000;
const SCOPES = "pages_show_list,pages_read_engagement,pages_manage_posts";
const CONTENT_TASKS = new Set(["CREATE_CONTENT", "MANAGE"]);
const VERSION = "2026-10-31";

interface PageGrant {
  id: string;
  name: string;
  access_token: string;
}

interface AuthorizationResult {
  user: { id: string; name: string };
  pages: PageGrant[];
  pages_seen?: number;
}

interface TransactionRow {
  id: string;
  state: string;
  challenge: string;
  status: string;
  sealed_result: string | null;
  expires_at: number;
}

class HttpError extends Error {
  constructor(readonly status: number, readonly code: string, message: string) {
    super(message);
  }
}

function json(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "Content-Type": "application/json; charset=utf-8", "Cache-Control": "no-store" },
  });
}

function html(body: string, status = 200): Response {
  return new Response(body, {
    status,
    headers: {
      "Content-Type": "text/html; charset=utf-8",
      "Cache-Control": "no-store",
      "Content-Security-Policy": "default-src 'none'; style-src 'unsafe-inline'",
    },
  });
}

async function body<T>(request: Request): Promise<T> {
  try {
    return (await request.json()) as T;
  } catch {
    throw new HttpError(400, "BAD_REQUEST", "Cuerpo JSON inválido.");
  }
}

function requireString(value: unknown, name: string): string {
  if (typeof value !== "string" || value.length === 0 || value.length > 2048)
    throw new HttpError(400, "BAD_REQUEST", `Falta ${name}.`);
  return value;
}

function graphFailure(error: GraphError | undefined, createsRemoteResource = false): HttpError {
  if (!error) return new HttpError(502, "UPSTREAM", "Facebook no respondió como se esperaba.");
  if (error.code === 190 || error.code === 102)
    return new HttpError(401, "AUTH_REQUIRED", "Necesitamos que vuelvas a autorizar Facebook.");
  if (error.code === 1 || error.code === 2 || error.code === 4 || error.code === 17)
    return new HttpError(createsRemoteResource ? 502 : 503, "UPSTREAM", `Facebook falló temporalmente: ${error.message}`);
  return new HttpError(400, "META_ERROR", error.message);
}

function callbackUrl(env: Env): string {
  return `${env.PUBLIC_BASE_URL}/v1/auth/meta/callback`;
}

async function cleanup(env: Env): Promise<void> {
  const now = Date.now();
  await env.DB.batch([
    env.DB.prepare("DELETE FROM transactions WHERE expires_at < ?").bind(now),
    env.DB.prepare("DELETE FROM operations WHERE expires_at < ?").bind(now),
  ]);
}

async function transactionFor(env: Env, id: string, verifier: unknown): Promise<TransactionRow> {
  const row = await env.DB.prepare("SELECT * FROM transactions WHERE id = ?").bind(id).first<TransactionRow>();
  if (!row) throw new HttpError(404, "NOT_FOUND", "La autorización no existe o ya venció.");
  const challenge = await sha256Base64Url(requireString(verifier, "verifier"));
  if (!constantTimeEqual(challenge, row.challenge)) throw new HttpError(403, "FORBIDDEN", "Verificador inválido.");
  if (row.expires_at < Date.now()) return { ...row, status: "expired" };
  return row;
}

async function grantedPageIds(graph: Graph, userToken: string): Promise<string[]> {
  const info = (await graph.debugToken(userToken)).data.data;
  const ids = new Set<string>();
  for (const grant of info?.granular_scopes ?? []) {
    if (grant.scope === "pages_manage_posts" || grant.scope === "pages_show_list")
      for (const id of grant.target_ids ?? []) ids.add(id);
  }
  return [...ids];
}

async function createTransaction(request: Request, env: Env): Promise<Response> {
  const { challenge } = await body<{ challenge?: string }>(request);
  if (typeof challenge !== "string" || !/^[A-Za-z0-9_-]{43}$/.test(challenge))
    throw new HttpError(400, "BAD_REQUEST", "Challenge inválido.");
  await cleanup(env);
  const id = randomToken(16);
  const state = randomToken(24);
  const now = Date.now();
  await env.DB.prepare(
    "INSERT INTO transactions (id, state, challenge, status, created_at, expires_at) VALUES (?, ?, ?, 'pending', ?, ?)",
  )
    .bind(id, state, challenge, now, now + TRANSACTION_TTL_MS)
    .run();
  const authorization = new URL(`https://www.facebook.com/${env.META_GRAPH_VERSION}/dialog/oauth`);
  const login: Record<string, string> = env.META_LOGIN_CONFIG_ID ? { config_id: env.META_LOGIN_CONFIG_ID } : { scope: SCOPES };
  authorization.search = new URLSearchParams({
    client_id: env.META_APP_ID,
    redirect_uri: callbackUrl(env),
    state,
    response_type: "code",
    ...login,
  }).toString();
  return json({ transaction_id: id, authorization_url: authorization.toString(), expires_at: new Date(now + TRANSACTION_TTL_MS).toISOString() });
}

async function completeCallback(url: URL, env: Env): Promise<Response> {
  const state = url.searchParams.get("state") ?? "";
  const row = await env.DB.prepare("SELECT * FROM transactions WHERE state = ?").bind(state).first<TransactionRow>();
  if (!row || row.status !== "pending" || row.expires_at < Date.now())
    return html(resultPage("La autorización venció o ya se usó. Volvé a OBS e intentá de nuevo."), 400);

  const code = url.searchParams.get("code");
  if (!code) {
    await env.DB.prepare("UPDATE transactions SET status = 'denied' WHERE id = ?").bind(row.id).run();
    return html(resultPage("No se concedió el permiso. Podés cerrar esta pestaña y volver a OBS."));
  }

  const graph = new Graph(env);
  const exchanged = await graph.exchangeCode(code, callbackUrl(env));
  const shortLived = exchanged.data.access_token;
  if (!shortLived) return html(resultPage("Facebook no completó la autorización. Volvé a OBS e intentá de nuevo."), 502);
  const longLived = (await graph.longLived(shortLived)).data.access_token ?? shortLived;

  const me = await graph.call<{ id: string; name: string }>("GET", "/me", longLived, { fields: "id,name" });
  const accounts = await graph.call<{ data?: Array<{ id: string; name: string; access_token: string; tasks?: string[] }> }>(
    "GET",
    "/me/accounts",
    longLived,
    { fields: "id,name,access_token,tasks", limit: "100" },
  );
  if (!me.ok || !accounts.ok) return html(resultPage("No pudimos leer tus páginas. Volvé a OBS e intentá de nuevo."), 502);

  const listed = (accounts.data.data ?? []).filter(
    (page) => page.access_token && (!page.tasks || page.tasks.some((task) => CONTENT_TASKS.has(task))),
  );
  const grantedIds = await grantedPageIds(graph, longLived);
  const missing = grantedIds.filter((id) => !listed.some((page) => page.id === id));
  const fetched = await Promise.all(
    missing.map((id) => graph.call<{ id: string; name: string; access_token?: string }>("GET", `/${id}`, longLived, { fields: "id,name,access_token" })),
  );
  const seen = [...listed, ...fetched.filter((page) => page.ok && page.data.access_token).map((page) => page.data)];
  const pages = seen.map(({ id, name, access_token }) => ({ id, name, access_token: access_token ?? "" }));
  const result: AuthorizationResult = { user: { id: me.data.id, name: me.data.name }, pages, pages_seen: seen.length };
  await env.DB.prepare("UPDATE transactions SET status = 'ready', sealed_result = ? WHERE id = ?")
    .bind(await seal(env.CACHE_ENCRYPTION_KEY, result), row.id)
    .run();
  return html(resultPage("Listo. Podés cerrar esta pestaña y volver a OBS."));
}

async function transactionStatus(id: string, request: Request, env: Env): Promise<Response> {
  const row = await transactionFor(env, id, (await body<{ verifier?: string }>(request)).verifier);
  return json({ status: row.status });
}

async function readyResult(id: string, verifier: unknown, env: Env): Promise<{ row: TransactionRow; result: AuthorizationResult }> {
  const row = await transactionFor(env, id, verifier);
  if (row.status !== "ready" || !row.sealed_result) throw new HttpError(409, "NOT_READY", "La autorización todavía no está lista.");
  return { row, result: await unseal<AuthorizationResult>(env.CACHE_ENCRYPTION_KEY, row.sealed_result) };
}

async function transactionResult(id: string, request: Request, env: Env): Promise<Response> {
  const { result } = await readyResult(id, (await body<{ verifier?: string }>(request)).verifier, env);
  return json({
    user: result.user,
    pages: result.pages.map(({ id: pageId, name }) => ({ id: pageId, name })),
    pages_seen: result.pages_seen ?? result.pages.length,
  });
}

async function selectPage(id: string, request: Request, env: Env): Promise<Response> {
  const payload = await body<{ verifier?: string; page_id?: string }>(request);
  const { result } = await readyResult(id, payload.verifier, env);
  const page = result.pages.find((candidate) => candidate.id === payload.page_id);
  if (!page) throw new HttpError(404, "NOT_FOUND", "Esa página no está entre las autorizadas.");
  return json({ user_id: result.user.id, page_id: page.id, page_name: page.name, page_token: page.access_token });
}

async function acknowledge(id: string, request: Request, env: Env): Promise<Response> {
  await transactionFor(env, id, (await body<{ verifier?: string }>(request)).verifier);
  await env.DB.prepare("DELETE FROM transactions WHERE id = ?").bind(id).run();
  return json({ ok: true });
}

async function checkPage(request: Request, env: Env): Promise<Response> {
  const payload = await body<{ page_token?: string; page_id?: string }>(request);
  const token = requireString(payload.page_token, "page_token");
  const pageId = requireString(payload.page_id, "page_id");
  const graph = new Graph(env);
  const debug = await graph.debugToken(token);
  const info = debug.data.data;
  if (!info?.is_valid || info.app_id !== env.META_APP_ID) throw new HttpError(401, "AUTH_REQUIRED", "Necesitamos que vuelvas a autorizar Facebook.");
  const missing = SCOPES.split(",").filter((scope) => !(info.scopes ?? []).includes(scope));
  if (missing.length) throw new HttpError(401, "AUTH_REQUIRED", `Faltan permisos de Facebook: ${missing.join(", ")}.`);
  const page = await graph.call<{ id: string; name: string }>("GET", `/${encodeURIComponent(pageId)}`, token, { fields: "id,name" });
  if (!page.ok) throw graphFailure(page.error);
  return json({ page_id: page.data.id, page_name: page.data.name });
}

async function createLive(request: Request, env: Env): Promise<Response> {
  const key = request.headers.get("Idempotency-Key");
  if (!key || key.length > 128) throw new HttpError(400, "BAD_REQUEST", "Falta Idempotency-Key.");
  const cached = await env.DB.prepare("SELECT sealed_response FROM operations WHERE idempotency_key = ?").bind(key).first<{ sealed_response: string }>();
  if (cached) return json(await unseal(env.CACHE_ENCRYPTION_KEY, cached.sealed_response));

  const payload = await body<{ page_token?: string; page_id?: string; title?: string; description?: string }>(request);
  const token = requireString(payload.page_token, "page_token");
  const pageId = requireString(payload.page_id, "page_id");
  const title = (payload.title ?? "").slice(0, 254);
  const description = (payload.description ?? "").slice(0, 5000);
  const graph = new Graph(env);
  const created = await graph.call<{ id: string; secure_stream_url?: string; stream_url?: string }>(
    "POST",
    `/${encodeURIComponent(pageId)}/live_videos`,
    token,
    { status: "LIVE_NOW", title, ...(description ? { description } : {}) },
  );
  if (!created.ok) {
    console.log(JSON.stringify({ event: "create_live_failed", code: created.error?.code, subcode: created.error?.subcode, message: created.error?.message }));
    throw graphFailure(created.error, true);
  }

  const details = await graph.call<{ permalink_url?: string; video?: { id: string } }>("GET", `/${created.data.id}`, token, {
    fields: "permalink_url,video",
  });
  const response = {
    id: created.data.id,
    secure_stream_url: created.data.secure_stream_url ?? created.data.stream_url ?? "",
    permalink_url: details.data.permalink_url ? new URL(details.data.permalink_url, "https://www.facebook.com").toString() : "",
    video_id: details.data.video?.id ?? "",
  };
  const now = Date.now();
  await env.DB.prepare(
    "INSERT OR REPLACE INTO operations (idempotency_key, live_video_id, sealed_response, created_at, expires_at) VALUES (?, ?, ?, ?, ?)",
  )
    .bind(key, response.id, await seal(env.CACHE_ENCRYPTION_KEY, response), now, now + OPERATION_TTL_MS)
    .run();
  return json(response);
}

async function liveStatus(id: string, request: Request, env: Env): Promise<Response> {
  const token = requireString((await body<{ page_token?: string }>(request)).page_token, "page_token");
  const result = await new Graph(env).call<{ status?: string; permalink_url?: string }>("GET", `/${encodeURIComponent(id)}`, token, {
    fields: "status,permalink_url",
  });
  if (!result.ok) throw graphFailure(result.error);
  return json({
    status: result.data.status ?? "",
    permalink_url: result.data.permalink_url ? new URL(result.data.permalink_url, "https://www.facebook.com").toString() : "",
  });
}

async function hideVideo(graph: Graph, token: string, videoId: string): Promise<{ hidden: boolean; method?: string; attempts: string[] }> {
  const attempts: string[] = [];
  const strategies: Array<[string, Record<string, string>]> = [
    ["privacy:SELF", { privacy: JSON.stringify({ value: "SELF" }) }],
    ["published:false", { published: "false" }],
  ];
  for (const [method, params] of strategies) {
    const result = await graph.call<{ success?: boolean }>("POST", `/${encodeURIComponent(videoId)}`, token, params);
    if (result.ok) return { hidden: true, method, attempts };
    attempts.push(`${method}: ${result.error?.message ?? result.status}`);
  }
  return { hidden: false, attempts };
}

async function endLive(id: string, request: Request, env: Env): Promise<Response> {
  const payload = await body<{ page_token?: string; hide_after_end?: boolean }>(request);
  const token = requireString(payload.page_token, "page_token");
  const graph = new Graph(env);
  const ended = await graph.call<{ id?: string }>("POST", `/${encodeURIComponent(id)}`, token, { end_live_video: "true" });
  if (!ended.ok) {
    const current = await graph.call<{ status?: string }>("GET", `/${encodeURIComponent(id)}`, token, { fields: "status" });
    const alreadyOver = current.ok && ["VOD", "LIVE_STOPPED", "PROCESSING"].includes(current.data.status ?? "");
    if (!alreadyOver) throw graphFailure(ended.error);
  }
  if (!payload.hide_after_end) return json({ ended: true, hidden: false });

  const details = await graph.call<{ video?: { id: string } }>("GET", `/${encodeURIComponent(id)}`, token, { fields: "video" });
  const videoId = details.data.video?.id;
  if (!videoId) return json({ ended: true, hidden: false, hide_error: "Facebook no devolvió el video del directo." });
  const hidden = await hideVideo(graph, token, videoId);
  console.log(JSON.stringify({ event: "hide_video", hidden: hidden.hidden, method: hidden.method ?? null, attempts: hidden.attempts }));
  return json({ ended: true, hidden: hidden.hidden, hidden_via: hidden.method, video_id: videoId, hide_error: hidden.hidden ? undefined : hidden.attempts.join(" | ") });
}

async function route(request: Request, env: Env): Promise<Response> {
  const url = new URL(request.url);
  const path = url.pathname;
  const method = request.method;

  if (method === "GET" && path === "/health") return json({ ok: true, version: VERSION, graph: env.META_GRAPH_VERSION });
  if (method === "GET" && path === "/") return html(homePage());
  if (method === "GET" && path === "/privacy") return html(privacyPage(env.PUBLIC_BASE_URL));
  if (method === "GET" && path === "/terms") return html(termsPage());
  if (method === "GET" && path === "/data-deletion") return html(dataDeletionPage());
  if (method === "GET" && path === "/v1/auth/meta/callback") return completeCallback(url, env);
  if (method !== "POST") throw new HttpError(404, "NOT_FOUND", "Ruta desconocida.");

  if (path === "/v1/auth/meta/transactions") return createTransaction(request, env);
  if (path === "/v1/meta/pages/check") return checkPage(request, env);
  if (path === "/v1/meta/live") return createLive(request, env);

  const transaction = path.match(/^\/v1\/auth\/meta\/transactions\/([A-Za-z0-9_-]+)\/(status|result|select|ack)$/);
  if (transaction) {
    const [, id, action] = transaction;
    if (action === "status") return transactionStatus(id, request, env);
    if (action === "result") return transactionResult(id, request, env);
    if (action === "select") return selectPage(id, request, env);
    return acknowledge(id, request, env);
  }

  const live = path.match(/^\/v1\/meta\/live\/([0-9]+)\/(status|end)$/);
  if (live) {
    const [, id, action] = live;
    return action === "status" ? liveStatus(id, request, env) : endLive(id, request, env);
  }
  throw new HttpError(404, "NOT_FOUND", "Ruta desconocida.");
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    try {
      return await route(request, env);
    } catch (error) {
      if (error instanceof HttpError) return json({ code: error.code, message: error.message }, error.status);
      console.error("unhandled", error instanceof Error ? error.name : "unknown");
      return json({ code: "INTERNAL", message: "Error interno del servicio." }, 500);
    }
  },

  async scheduled(_controller: ScheduledController, env: Env): Promise<void> {
    await cleanup(env);
  },
};
