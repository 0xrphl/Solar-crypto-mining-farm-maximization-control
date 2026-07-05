-- ============================================================
-- Supabase Schema v3 — MINIMAL for free tier
-- solar-cluster (obzzoqghurezcytjzpgq)
-- Run in Supabase SQL Editor
--
-- Bi-phase: All three quantities directly measured
--   Solar = A1 + |B2|  (solar CT clamps)
--   Grid  = A2 + C2    (grid CT clamps, negative=exporting)
--   House = |B1| + |C1| (house CT clamps, incl. shower)
--
-- Refoss EM06P reports ACTIVE power (W), not apparent (VA).
-- PF is reported separately. No PF multiplication needed.
--
-- ~288 rows/day × 16 cols × 4B ≈ 18 KB/day ≈ 550 KB/month
-- ============================================================

CREATE TABLE IF NOT EXISTS energy (
    id   BIGSERIAL    PRIMARY KEY,
    ts   TIMESTAMPTZ  NOT NULL DEFAULT now(),
    a1_w REAL, a1_pf REAL,  -- Solar Phase A (ch1)
    a2_w REAL, a2_pf REAL,  -- Grid Phase A (ch4)
    b1_w REAL, b1_pf REAL,  -- House Phase B (ch2)
    b2_w REAL, b2_pf REAL,  -- Solar Phase B (ch5)
    c1_w REAL, c1_pf REAL,  -- Shower / House (ch3)
    c2_w REAL, c2_pf REAL,  -- Grid Phase B (ch6)
    solar REAL,              -- A1+|B2| (direct measurement)
    grid  REAL,              -- A2+C2 (direct measurement, +=import, -=export)
    home  REAL,              -- |B1|+|C1| (direct measurement)
    n     INT                -- samples in avg
);
CREATE INDEX IF NOT EXISTS idx_energy_ts ON energy (ts DESC);

-- 16 mining profiles (reference/lookup)
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

-- Mining state transitions log (only when state changes)
-- When old_id == new_id, it's a CORRECTION (power outage recovery)
CREATE TABLE IF NOT EXISTS transitions (
    id      BIGSERIAL    PRIMARY KEY,
    ts      TIMESTAMPTZ  NOT NULL DEFAULT now(),
    old_id  INT REFERENCES profiles(id),
    new_id  INT REFERENCES profiles(id),
    surplus REAL,   -- available surplus at decision time
    grid    REAL    -- avg grid at decision time
);
CREATE INDEX IF NOT EXISTS idx_trans_ts ON transitions (ts DESC);

CREATE TABLE IF NOT EXISTS config (
    key TEXT PRIMARY KEY, value JSONB NOT NULL
);
INSERT INTO config (key, value) VALUES
    ('thresholds', '{"max_grid_w":3000,"solar_min_w":200}'),
    ('schedule', '{"peak":[18,19,20,21,22]}'),
    ('auto', '{"enabled":true}')
ON CONFLICT (key) DO NOTHING;

ALTER TABLE energy ENABLE ROW LEVEL SECURITY;
ALTER TABLE profiles ENABLE ROW LEVEL SECURITY;
ALTER TABLE transitions ENABLE ROW LEVEL SECURITY;
ALTER TABLE config ENABLE ROW LEVEL SECURITY;
CREATE POLICY "e_ins" ON energy FOR INSERT WITH CHECK (true);
CREATE POLICY "e_sel" ON energy FOR SELECT USING (true);
CREATE POLICY "p_sel" ON profiles FOR SELECT USING (true);
CREATE POLICY "t_ins" ON transitions FOR INSERT WITH CHECK (true);
CREATE POLICY "t_sel" ON transitions FOR SELECT USING (true);
CREATE POLICY "c_sel" ON config FOR SELECT USING (true);
CREATE POLICY "c_upd" ON config FOR UPDATE USING (true) WITH CHECK (true);

CREATE OR REPLACE VIEW latest AS
SELECT * FROM energy ORDER BY ts DESC LIMIT 1;

CREATE OR REPLACE VIEW hourly AS
SELECT date_trunc('hour',ts) AS h,
  round(avg(solar)::numeric,0) AS solar,
  round(avg(grid)::numeric,0)  AS grid,
  round(avg(home)::numeric,0)  AS home,
  count(*) AS n
FROM energy GROUP BY 1 ORDER BY 1 DESC;

CREATE OR REPLACE VIEW daily AS
SELECT date_trunc('day',ts) AS d,
  round(avg(solar)::numeric,0) AS solar,
  round(avg(grid)::numeric,0)  AS grid,
  round(avg(home)::numeric,0)  AS home,
  count(*) AS n
FROM energy GROUP BY 1 ORDER BY 1 DESC;
