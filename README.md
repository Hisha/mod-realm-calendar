# mod-realm-calendar

`mod-realm-calendar` is a standalone AzerothCore module that automatically publishes the realm's **public in-game calendar** as a machine-readable JSON feed for launchers, websites, and other external consumers.

The module intentionally excludes player-created calendar events, raid lockouts, guild invitations, character reminders, and other personal calendar data. Editorial/admin news belongs in a separate realm-news service.

## Version 1.0.0

The 1.0 release provides a self-maintaining rolling public calendar feed with client-facing holiday dates, weekly fishing contest times, DST-aware timestamps, atomic file replacement, and administrator diagnostics.

## Automatic rolling publication

Normal operation requires no GM command, cron job, or external scheduler. When enabled, the module:

- publishes the JSON feed automatically when `worldserver` starts;
- checks periodically for a month rollover or a missing output file;
- regenerates when a new month begins, dropping the expired month and adding another month at the far end;
- republishes after a configuration reload;
- writes to a temporary file and replaces the public JSON only after the new file is complete.

`RealmCalendar.FutureMonths = 12` means **the current month plus twelve additional months**. On September 4, 2026 the published range is September 1, 2026 through September 30, 2027. On October 1 it becomes October 1, 2026 through October 31, 2027.

If the server is offline when the month changes, the next startup immediately publishes the correct rolling range.

## Calendar sources

The public schedule is built from the same runtime data AzerothCore uses:

- seasonal/date-driven holidays use the in-memory `Holidays.dbc` definitions after AzerothCore has populated their dynamic dates;
- weekly fishing contests use their `GameEventMgr` recurrence for timing while retaining their holiday identity;
- weekly recurrence is advanced in realm wall-clock time so 14:00 events remain at 14:00 across DST transitions;
- Call to Arms / `CalendarFilterType 2` events are excluded from the default public feed.

The September 2026 regression set was verified against the WoW 3.3.5a client for Darkmoon Faire, Kalu'ak Fishing Derby, Stranglethorn Fishing Extravaganza, Pirates' Day, Brewfest, and Harvest Festival.

## Configuration

AzerothCore installs `conf/mod_realm_calendar.conf.dist` through the normal module configuration process.

```ini
RealmCalendar.Enable = 1
RealmCalendar.OutputFile = "calendar.json"
RealmCalendar.FutureMonths = 12
RealmCalendar.CheckIntervalMinutes = 60
RealmCalendar.Diagnostics = 0
RealmCalendar.DefaultHorizonDays = 30
RealmCalendar.MaxHorizonDays = 730
```

For a web-served feed, set `RealmCalendar.OutputFile` to an absolute path writable by the worldserver service account, for example a directory exposed by nginx or Apache. Parent directories are created automatically when possible.

`RealmCalendar.Diagnostics` is intentionally disabled by default in 1.0. Production publication does not require the diagnostic commands.

## JSON schema

The generated document uses `schemaVersion: 1` and records when it was generated, its inclusive date range, and the public events. Multi-day holidays are represented as date-only/all-day events; contests preserve explicit ISO-8601 timestamps with the realm's UTC offset.

```json
{
  "schemaVersion": 1,
  "generatedAt": "2026-09-05T02:31:00Z",
  "range": {
    "start": "2026-09-01",
    "end": "2027-09-30"
  },
  "events": [
    {
      "holidayId": 424,
      "gameEventId": 64,
      "name": "Kalu'ak Fishing Derby",
      "category": "fishing",
      "allDay": false,
      "start": "2026-09-12T14:00:00-04:00",
      "end": "2026-09-12T15:00:00-04:00",
      "texture": "Calendar_FishingExtravaganza"
    },
    {
      "holidayId": 374,
      "gameEventId": 4,
      "name": "Darkmoon Faire",
      "category": "holiday",
      "allDay": true,
      "startDate": "2026-09-06",
      "endDate": "2026-09-12",
      "texture": "Calendar_DarkmoonFaireElwynn"
    }
  ]
}
```

`startDate` and `endDate` are inclusive for all-day events. Timed events use `start` and `end` timestamps.

## Administrator commands

Normal operation is automatic. Two production-safe commands remain available:

- `.realmcalendar status` — show module version, enable state, output-file state, rolling range, and check interval.
- `.realmcalendar publish` — force an immediate JSON regeneration.

When `RealmCalendar.Diagnostics = 1`, the following troubleshooting commands are also available:

- `.realmcalendar month <year> <month>` — compact client-style month view for regression testing.
- `.realmcalendar upcoming [days]` — calculated public occurrences in a future window.
- `.realmcalendar holidays` — logical `Holidays.dbc` definitions and stages.
- `.realmcalendar inspect` — raw calendar-facing `GameEventMgr` rows.

## Installation

Place the repository under AzerothCore's `modules` directory, rerun CMake, rebuild/install, configure the output path, and restart `worldserver`.

```bash
cd ~/azerothcore-wotlk/build
cmake ..
make -j"$(nproc)"
make install
```

After startup, check the worldserver log for a `[Realm Calendar] Published ...` line and inspect the configured JSON file. `.realmcalendar status` provides a quick runtime sanity check.

## Output and consumers

The module stays consumer-neutral. Portalkeeper, a website, or another launcher can consume the same feed. The JSON file is an output artifact and should not be treated as configuration or source data.

## Scope

Included: realm/public holiday and scheduled-event information represented by AzerothCore's calendar/event system.

Excluded: player-created events, personal reminders, guild invitations, raid lockouts, character-specific data, and editorial realm news.

## License

GPL-2.0-or-later, matching AzerothCore's licensing model.
