export interface Env {
  DB: D1Database;
  META_APP_ID: string;
  META_LOGIN_CONFIG_ID?: string;
  META_APP_SECRET: string;
  META_GRAPH_VERSION: string;
  PUBLIC_BASE_URL: string;
  CACHE_ENCRYPTION_KEY: string;
}
