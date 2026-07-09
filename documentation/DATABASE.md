# 🗄️ Database Schema & Queries (Supabase) — v4

## Overview

Schema optimized for Supabase free tier (500 MB limit).
Estimated storage: ~7 MB/year → **~70 years** before hitting the limit.

| Table | Purpose | Rows/month | Size/year |
|-------|---------|-----------|-----------|
| `energy` | 9-min averaged readings (per-phase + VA) | 4,800 | ~7 MB |
| `transitions` | Mining state changes | ~300-900 | ~0.2 MB |
| `profiles` | 16 mining combos (static) | 16 | 0 |
| `config` | Settings/thresholds | 3 | 0 |

## Entity Relationship

```
┌─────────────┐       ┌──────────────┐
│   profiles   │       │    config     │
├─────────────┤       ├──────────────┤
│ id (PK)     │◄──┐   │ key (PK)     │
│ name        │   │   │ value (JSONB) │
│ w           │   │   └──────────────┘
│ r1          │   │
│ r2          │   │   ┌──────────────────────┐
│ avalon      │   │   │       energy          │
└─────────────┘   │   ├──────────────────────┤
                  │   │ id (PK), ts           │
                  │   │                       │
                  │   │ ── Per-channel ──      │
                  │   │ a1_w, a1_pf           │  ← Solar P1
                  │   │ a2_w, a2_pf           │  ← Grid P1
                  │   │ b1_w, b1_pf           │  ← House P2
                  │   │ b2_w, b2_pf           │  ← Solar P2
                  │   │ c1_w, c1_pf           │  ← Shower
                  │   │ c2_w, c2_pf           │  ← Grid P2
                  │   │                       │
                  │   │ ── System totals W ──  │
                  │   │ solar, grid, home      │
                  │   │                       │
                  │   │ ── Per-phase W ──      │  (v4)
                  │   │ solar_p1, solar_p2     │
                  │   │ grid_p1, grid_p2       │
                  │   │ home_p1, home_p2       │
                  │   │                       │
                  │   │ ── Apparent VA ──      │  (v4)
                  │   │ solar_va, grid_va      │
                  │   │ home_va                │
                  │   │                       │
                  │   │ ── Surplus ──          │  (v4)
                  │   │ saved_w, saved_va      │
                  │   │                       │
                  │   │ ── Mining state ──     │  (v4)
                  ├───│ profile_id (FK) ───────┤
                  │   │ n (sample count)       │
                  │   └──────────────────────┘
                  │
            ┌─────┴──────────────┐
            │    transitions      │
            ├────────────────────┤
            │ id (PK)            │
            │ ts                 │
            │ old_id (FK)        │  ← old_id == new_id means CORRECTION
            │ new_id (FK)        │
            │ surplus            │
            │ grid               │
            └────────────────────┘
```

## Column Details

### `energy` — 9-minute averaged readings (~160/day)

#### Per-channel (original v3)

| Column | Type | Description |
|--------|------|-------------|
| `ts` | TIMESTAMPTZ | Timestamp (auto) |
| `a1_w`, `a1_pf` | REAL | Solar Phase 1 (watts, power factor) |
| `a2_w`, `a2_pf` | REAL | Grid Phase 1 |
| `b1_w`, `b1_pf` | REAL | House Phase 2 |
| `b2_w`, `b2_pf` | REAL | Solar Phase 2 |
| `c1_w`, `c1_pf` | REAL | Shower |
| `c2_w`, `c2_pf` | REAL | Grid Phase 2 |

#### System totals (original v3)

| Column | Type | Description |
|--------|------|-------------|
| `solar` | REAL | Total solar W: \|A1\| + \|B2\| |
| `grid` | REAL | Total grid W: A2 + C2 (+=import, −=export) |
| `home` | REAL | Total home W: Phase1(calc) + Phase2(meas) |

#### Per-phase breakdown (v4 — NEW)

| Column | Type | Description |
|--------|------|-------------|
| `solar_p1` | REAL | Phase 1 solar W: \|A1\| (measured) |
| `solar_p2` | REAL | Phase 2 solar W: \|B2\| (measured) |
| `grid_p1` | REAL | Phase 1 grid W: A2 (signed) |
| `grid_p2` | REAL | Phase 2 grid W: C2 (signed) |
| `home_p1` | REAL | Phase 1 home W: solar_p1 + grid_p1 (**calculated**, floor 0) |
| `home_p2` | REAL | Phase 2 home W: \|B1\| + \|C1\| (**directly measured**) |

#### Apparent power (v4 — NEW)

| Column | Type | Description |
|--------|------|-------------|
| `solar_va` | REAL | Total solar apparent VA |
| `grid_va` | REAL | Total grid apparent VA |
| `home_va` | REAL | Total home apparent VA |

#### Surplus & state (v4 — NEW)

| Column | Type | Description |
|--------|------|-------------|
| `saved_w` | REAL | Active surplus: solar − home (W) |
| `saved_va` | REAL | Apparent surplus: solar_va − home_va (VA) |
| `profile_id` | INT | Mining profile active at snapshot (0-15) |
| `n` | INT | Number of 30s samples in this average |

### `transitions` — Mining state changes

| Column | Type | Description |
|--------|------|-------------|
| `ts` | TIMESTAMPTZ | When switch happened |
| `old_id` | INT FK→profiles | Previous profile |
| `new_id` | INT FK→profiles | New profile (== old_id for corrections) |
| `surplus` | REAL | Available surplus (W) |
| `grid` | REAL | Average grid reading (W) |

### `config` — System settings

| Key | Default | Description |
|-----|---------|-------------|
| `thresholds` | `{"max_grid_w":3000,"solar_min_w":200}` | Safety limits |
| `schedule` | `{"peak":[18,19,20,21,22]}` | Peak hours |
| `auto` | `{"enabled":true}` | Automation toggle |

## Views

```sql
SELECT * FROM latest;                                    -- Current reading
SELECT * FROM hourly WHERE h > now() - interval '24h';   -- Hourly averages (W + VA)
SELECT * FROM daily WHERE d > now() - interval '30d';    -- Daily averages
SELECT * FROM daily_phases WHERE d > now() - interval '7d'; -- Per-phase daily
SELECT * FROM profile_usage;                              -- Mining profile hours
```

## Dashboard Queries

```sql
-- Today's mining transitions
SELECT t.ts::time as time, p1.name as "from", p2.name as "to",
       p2.w || 'W' as mining, t.surplus::int || 'W' as surplus
FROM transitions t
JOIN profiles p1 ON t.old_id = p1.id
JOIN profiles p2 ON t.new_id = p2.id
WHERE t.ts::date = current_date ORDER BY t.ts;

-- Corrections (power outage recoveries)
SELECT ts, surplus, grid FROM transitions
WHERE old_id = new_id AND ts > now() - interval '7 days';

-- Peak solar hour today
SELECT h, solar as peak_solar_w, solar_va as peak_solar_va FROM hourly
WHERE h::date = current_date ORDER BY solar DESC LIMIT 1;

-- Daily trend (last 7 days) with surplus
SELECT d::date, solar as avg_solar, home as avg_home, grid as avg_grid,
       saved as avg_saved
FROM daily ORDER BY d DESC LIMIT 7;

-- Per-phase balance (today)
SELECT ts::time, solar_p1, solar_p2, grid_p1, grid_p2, home_p1, home_p2,
       saved_w, profile_id
FROM energy WHERE ts::date = current_date ORDER BY ts;

-- Apparent vs Active power comparison
SELECT ts::time, solar, solar_va, grid, grid_va, home, home_va
FROM energy WHERE ts::date = current_date ORDER BY ts;

-- Profile usage frequency (last 30 days)
SELECT p.name, p.w || 'W' as watts, count(*) as times_entered
FROM transitions t JOIN profiles p ON t.new_id = p.id
WHERE t.ts > now() - interval '30 days'
GROUP BY p.name, p.w ORDER BY count(*) DESC;

-- Profile time distribution (from energy snapshots)
SELECT * FROM profile_usage;

-- Audit: last 3 days full data
SELECT ts, a1_w, a2_w, b1_w, b2_w, c1_w, c2_w,
       solar, grid, home, saved_w, solar_va, grid_va, home_va,
       profile_id
FROM energy WHERE ts > now() - interval '3 days' ORDER BY ts DESC;
```

## Setup

### Fresh Install
```sql
-- Run supabase_schema.sql in SQL Editor (creates all tables + views)
```

### Upgrade from v3 → v4
```sql
-- Run supabase_migration_v4.sql in SQL Editor
-- This adds new columns to existing energy table (preserves all data)
-- Then manually recreate views by running the VIEWS section from supabase_schema.sql
```

### Full Reset (drops all data)
```sql
DROP TABLE IF EXISTS energy, transitions, profiles, config CASCADE;
-- Then run supabase_schema.sql
```
