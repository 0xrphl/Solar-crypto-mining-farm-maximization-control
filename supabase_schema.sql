-- ============================================================
-- Supabase Schema v4 — Solar Mining Control
-- solar-cluster (obzzoqghurezcytjzpgq)
-- Run in Supabase SQL Editor
--
-- SAFE TO RUN MULTIPLE TIMES:
--   - CREATE TABLE IF NOT EXISTS (won't break existing tables)
--   - ALTER TABLE ADD COLUMN IF NOT EXISTS (adds new v4 columns)
--   - DROP VIEW + CREATE VIEW (recreates views cleanly)
--   - INSERT ON CONFLICT DO NOTHING (won't duplicate profiles)
--
-- 2-Phase (Bi-Phase) Circuit:
--   Phase 1 (A): Solar=A1, Grid=A2, Home=calculated (Solar+Grid)
--   Phase 2 (B/C): Solar=B2, Grid=C2, Home=|B1|+|C1| (measured)
--
-- Per-channel: Active W, Power Factor
-- Per-phase: Solar/Grid/Home W breakdown
-- System: Totals W + VA, Power Saved, Mining Profile
--
-- ~160 rows/day × 30 cols × 4B ≈ 19 KB/day ≈ 7 MB/year
-- Free tier 500MB = ~70 years of data
-- ============================================================

-- ==================== ENERGY TABLE ====================

CREATE TABLE IF NOT EXISTS energy (
    id   BIGSERIAL    PRIMARY KEY,
    ts   TIMESTAMPTZ  NOT NULL DEFAULT now(),

    -- Per-channel: active power (W) and power factor
    a1_w REAL, a1_pf REAL,  -- Solar Phase 1 (ch1)
    a2_w REAL, a2_pf REAL,  -- Grid Phase 1 (ch4)
    b1_w REAL, b1_pf REAL,  -- House Phase 2 (ch2)
    b2_w REAL, b2_pf REAL,  -- Solar Phase 2 (ch5)
    c1_w REAL, c1_pf REAL,  -- Shower / House load (ch3)
    c2_w REAL, c2_pf REAL,  -- Grid Phase 2 (ch6)

    -- System totals: active power (W)
    solar REAL,              -- |A1| + |B2| (total solar generation)
    grid  REAL,              -- A2 + C2 (signed: +=import, -=export)
    home  REAL,              -- Phase1(calc) + Phase2(meas) total consumption

    -- Aggregation metadata
    n     INT                -- Number of 30s samples in this average
);
CREATE INDEX IF NOT EXISTS idx_energy_ts ON energy (ts DESC);

-- v4 columns: add if upgrading from v3 (safe to run on fresh installs too)
ALTER TABLE energy ADD COLUMN IF NOT EXISTS solar_p1 REAL;   -- Phase 1 solar W (|A1|)
ALTER TABLE energy ADD COLUMN IF NOT EXISTS solar_p2 REAL;   -- Phase 2 solar W (|B2|)
ALTER TABLE energy ADD COLUMN IF NOT EXISTS grid_p1 REAL;    -- Phase 1 grid W (A2, signed)
ALTER TABLE energy ADD COLUMN IF NOT EXISTS grid_p2 REAL;    -- Phase 2 grid W (C2, signed)
ALTER TABLE energy ADD COLUMN IF NOT EXISTS home_p1 REAL;    -- Phase 1 home W (calculated: solar+grid)
ALTER TABLE energy ADD COLUMN IF NOT EXISTS home_p2 REAL;    -- Phase 2 home W (measured: |B1|+|C1|)
ALTER TABLE energy ADD COLUMN IF NOT EXISTS solar_va REAL;   -- Total solar apparent VA
ALTER TABLE energy ADD COLUMN IF NOT EXISTS grid_va REAL;    -- Total grid apparent VA
ALTER TABLE energy ADD COLUMN IF NOT EXISTS home_va REAL;    -- Total home apparent VA
ALTER TABLE energy ADD COLUMN IF NOT EXISTS saved_w REAL;    -- Active surplus: solar - home (W)
ALTER TABLE energy ADD COLUMN IF NOT EXISTS saved_va REAL;   -- Apparent surplus: solar_va - home_va (VA)
ALTER TABLE energy ADD COLUMN IF NOT EXISTS profile_id INT;  -- Mining profile active at snapshot (0-15)

-- ==================== PROFILES TABLE ====================

CREATE TABLE IF NOT EXISTS profiles (
    id   INT PRIMARY KEY,
    name TEXT NOT NULL,
    w    INT  NOT NULL,   -- total watts
    r1   BOOLEAN,         -- Relay 1: BitAxe+Nerdaxe
    r2   BOOLEAN,         -- Relay 2: Octaxe
    avalon TEXT            -- off/low/mid/high
);
INSERT INTO profiles VALUES
    (0,  'OFF',           0,    false, false, 'off'),
    (1,  'BN',            81,   true,  false, 'off'),
    (2,  'OCT',           180,  false, true,  'off'),
    (3,  'BN+OCT',        261,  true,  true,  'off'),
    (4,  'AV_LO',         800,  false, false, 'low'),
    (5,  'AV_LO+BN',      881,  true,  false, 'low'),
    (6,  'AV_LO+OCT',     980,  false, true,  'low'),
    (7,  'AV_LO+BN+OCT',  1061, true,  true,  'low'),
    (8,  'AV_MD',          1600, false, false, 'mid'),
    (9,  'AV_MD+BN',       1681, true,  false, 'mid'),
    (10, 'AV_HI',          1720, false, false, 'high'),
    (11, 'AV_MD+OCT',      1780, false, true,  'mid'),
    (12, 'AV_HI+BN',       1801, true,  false, 'high'),
    (13, 'AV_MD+BN+OCT',   1861, true,  true,  'mid'),
    (14, 'AV_HI+OCT',      1900, false, true,  'high'),
    (15, 'AV_HI+BN+OCT',   2001, true,  true,  'high')
ON CONFLICT (id) DO NOTHING;

-- ==================== TRANSITIONS TABLE ====================

CREATE TABLE IF NOT EXISTS transitions (
    id      BIGSERIAL    PRIMARY KEY,
    ts      TIMESTAMPTZ  NOT NULL DEFAULT now(),
    old_id  INT REFERENCES profiles(id),
    new_id  INT REFERENCES profiles(id),
    surplus REAL,   -- available surplus at decision time
    grid    REAL    -- avg grid at decision time
);
CREATE INDEX IF NOT EXISTS idx_trans_ts ON transitions (ts DESC);

-- ==================== CONFIG TABLE ====================

CREATE TABLE IF NOT EXISTS config (
    key TEXT PRIMARY KEY, value JSONB NOT NULL
);
INSERT INTO config (key, value) VALUES
    ('thresholds', '{"max_grid_w":3000,"solar_min_w":200}'),
    ('schedule', '{"peak":[18,19,20,21,22]}'),
    ('auto', '{"enabled":true}')
ON CONFLICT (key) DO NOTHING;

-- ==================== ROW LEVEL SECURITY ====================

ALTER TABLE energy ENABLE ROW LEVEL SECURITY;
ALTER TABLE profiles ENABLE ROW LEVEL SECURITY;
ALTER TABLE transitions ENABLE ROW LEVEL SECURITY;
ALTER TABLE config ENABLE ROW LEVEL SECURITY;

DO $$ BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_policies WHERE policyname = 'e_ins') THEN
        CREATE POLICY "e_ins" ON energy FOR INSERT WITH CHECK (true);
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_policies WHERE policyname = 'e_sel') THEN
        CREATE POLICY "e_sel" ON energy FOR SELECT USING (true);
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_policies WHERE policyname = 'p_sel') THEN
        CREATE POLICY "p_sel" ON profiles FOR SELECT USING (true);
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_policies WHERE policyname = 't_ins') THEN
        CREATE POLICY "t_ins" ON transitions FOR INSERT WITH CHECK (true);
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_policies WHERE policyname = 't_sel') THEN
        CREATE POLICY "t_sel" ON transitions FOR SELECT USING (true);
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_policies WHERE policyname = 'c_sel') THEN
        CREATE POLICY "c_sel" ON config FOR SELECT USING (true);
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_policies WHERE policyname = 'c_upd') THEN
        CREATE POLICY "c_upd" ON config FOR UPDATE USING (true) WITH CHECK (true);
    END IF;
END $$;

-- ==================== VIEWS ====================
-- Drop first to avoid column-rename conflicts on upgrades

DROP VIEW IF EXISTS latest CASCADE;
DROP VIEW IF EXISTS hourly CASCADE;
DROP VIEW IF EXISTS daily CASCADE;
DROP VIEW IF EXISTS daily_phases CASCADE;
DROP VIEW IF EXISTS profile_usage CASCADE;

-- Latest reading
CREATE VIEW latest AS
SELECT * FROM energy ORDER BY ts DESC LIMIT 1;

-- Hourly averages (W + VA)
CREATE VIEW hourly AS
SELECT date_trunc('hour', ts) AS h,
  round(avg(solar)::numeric, 0) AS solar,
  round(avg(grid)::numeric, 0)  AS grid,
  round(avg(home)::numeric, 0)  AS home,
  round(avg(saved_w)::numeric, 0) AS saved,
  round(avg(solar_va)::numeric, 0) AS solar_va,
  round(avg(grid_va)::numeric, 0)  AS grid_va,
  round(avg(home_va)::numeric, 0)  AS home_va,
  count(*) AS n
FROM energy GROUP BY 1 ORDER BY 1 DESC;

-- Daily averages
CREATE VIEW daily AS
SELECT date_trunc('day', ts) AS d,
  round(avg(solar)::numeric, 0) AS solar,
  round(avg(grid)::numeric, 0)  AS grid,
  round(avg(home)::numeric, 0)  AS home,
  round(avg(saved_w)::numeric, 0) AS saved,
  round(avg(solar_va)::numeric, 0) AS solar_va,
  count(*) AS n
FROM energy GROUP BY 1 ORDER BY 1 DESC;

-- Per-phase daily averages
CREATE VIEW daily_phases AS
SELECT date_trunc('day', ts) AS d,
  round(avg(solar_p1)::numeric, 0) AS solar_p1,
  round(avg(solar_p2)::numeric, 0) AS solar_p2,
  round(avg(grid_p1)::numeric, 0)  AS grid_p1,
  round(avg(grid_p2)::numeric, 0)  AS grid_p2,
  round(avg(home_p1)::numeric, 0)  AS home_p1,
  round(avg(home_p2)::numeric, 0)  AS home_p2,
  count(*) AS n
FROM energy GROUP BY 1 ORDER BY 1 DESC;

-- Mining profile usage: how long each profile was active
CREATE VIEW profile_usage AS
SELECT profile_id, p.name, p.w AS watts,
  count(*) AS readings,
  round(count(*) * 9.0 / 60, 1) AS hours_approx
FROM energy e
JOIN profiles p ON e.profile_id = p.id
WHERE profile_id IS NOT NULL
GROUP BY profile_id, p.name, p.w
ORDER BY profile_id;
