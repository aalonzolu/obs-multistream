CREATE TABLE IF NOT EXISTS transactions (
  id TEXT PRIMARY KEY,
  state TEXT NOT NULL UNIQUE,
  challenge TEXT NOT NULL,
  status TEXT NOT NULL,
  sealed_result TEXT,
  created_at INTEGER NOT NULL,
  expires_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS operations (
  idempotency_key TEXT PRIMARY KEY,
  live_video_id TEXT,
  sealed_response TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  expires_at INTEGER NOT NULL
);
