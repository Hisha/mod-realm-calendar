# Changelog

## 1.0.0 - 2026-09-04

- Added automatic rolling JSON publication for the current month plus a configurable number of future months.
- Added startup publication, month-rollover refresh, missing-file recovery, and config-reload refresh.
- Added atomic temporary-file publication to avoid partially written feeds.
- Added client-facing seasonal holiday calculation from AzerothCore's in-memory `Holidays.dbc` data.
- Added weekly Kalu'ak Fishing Derby and Stranglethorn Fishing Extravaganza recurrence handling.
- Preserved realm wall-clock times across DST transitions with ISO-8601 UTC offsets.
- Added all-day versus timed-event JSON semantics.
- Excluded Call to Arms events from the default public feed.
- Added `.realmcalendar status` and manual `.realmcalendar publish` administrator commands.
- Moved development diagnostics behind `RealmCalendar.Diagnostics`, disabled by default.
