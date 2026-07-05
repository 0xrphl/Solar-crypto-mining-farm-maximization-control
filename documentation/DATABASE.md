# 🗄️ Database Schema & Queries (Supabase)

## Overview

Minimal schema optimized for Supabase free tier (500 MB limit).
Estimated storage: ~6.2 MB/year → **~80 years** before hitting the limit.

| Table | Purpose | Rows/month | Size/year |
|-------|---------|-----------|-----------|
| `energy` | 5-min averaged readings | 8,640 | ~6 MB |
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
│ r2          │   │   ┌──────────────────┐
│ avalon      │   │   │     energy        │
└─────────────┘   │   ├──────────────────┤
                  │   │ id (PK)          │
                  │   │ ts               │
                  │   │ a1_w, a1_pf      │  ← Solar Phase A
                  │   │ a2_w, a2_pf      │  ← Grid Phase A
                  │   │ b1_w, b1_pf      │  ← House Phase B
                  │   │ b2_w, b2_pf      │  ← Solar Phase B
                  │   │ c1_w, c1_pf      │  ← Shower
                  │   │ c2_w, c2_pf      │  ← Grid Phase B
                  │   │ solar, grid, home │
                  │   │ n (sample count)  │
                  │   └──────────────────┘
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

### `energy` — 5-minute averaged readings (288/day)

| Column | Type | Description |
|--------|------|-------------|
| `ts` | TIMESTAMPTZ | Timestamp (auto) |
| `a1_w`, `a1_pf` | REAL | Solar Phase A (watts, power factor) |
| `a2_w`, `a2_pf` | REAL | Grid Phase A |
| `b1_w`, `b1_pf` | REAL | House Phase B |
| `b2_w`, `b2_pf` | REAL | Solar Phase B |
| `c1_w`, `c1_pf` | REAL | Shower |
| `c2_w`, `c2_pf` | REAL | Grid Phase B |
| `solar` | REAL | Total solar: A1+|B2| |
| `grid` | REAL | Grid: A2+C2 (+import/-export) |
| `home` | REAL | House: |B1|+|C1| |
| `n` | INT | Number of 30s samples in average |

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
SELECT * FROM hourly WHERE h > now() - interval '24h';   -- Hourly averages
SELECT * FROM daily WHERE d > now() - interval '30d';    -- Daily averages
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
SELECT h, solar as peak_solar_w FROM hourly
WHERE h::date = current_date ORDER BY solar DESC LIMIT 1;

-- Daily trend (last 7 days)
SELECT d::date, solar as avg_solar, home as avg_home, grid as avg_grid
FROM daily ORDER BY d DESC LIMIT 7;

-- Profile usage frequency (last 30 days)
SELECT p.name, p.w || 'W' as watts, count(*) as times_entered
FROM transitions t JOIN profiles p ON t.new_id = p.id
WHERE t.ts > now() - interval '30 days'
GROUP BY p.name, p.w ORDER BY count(*) DESC;

-- Audit: last 3 days energy data
SELECT ts, a1_w, a2_w, b1_w, b2_w, c1_w, c2_w, solar, grid, home
FROM energy WHERE ts > now() - interval '3 days' ORDER BY ts DESC;
```

## Setup

```sql
-- Fresh install: run supabase_schema.sql in SQL Editor
-- Upgrade: drop old tables first
DROP TABLE IF EXISTS energy, transitions, profiles, config CASCADE;
-- Then run supabase_schema.sql
```
