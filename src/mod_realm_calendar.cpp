#include "Chat.h"
#include "CommandScript.h"
#include "Config.h"
#include "DBCStores.h"
#include "GameEventMgr.h"
#include "HolidayDateCalculator.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
struct RealmCalendarConfig
{
    bool Enabled = true;
    uint32 DefaultHorizonDays = 30;
    uint32 MaxHorizonDays = 730;
};

RealmCalendarConfig gRealmCalendarConfig;

void LoadRealmCalendarConfig()
{
    gRealmCalendarConfig.Enabled = sConfigMgr->GetOption<bool>("RealmCalendar.Enable", true);
    gRealmCalendarConfig.DefaultHorizonDays = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("RealmCalendar.DefaultHorizonDays", 30));
    gRealmCalendarConfig.MaxHorizonDays = std::max<uint32>(gRealmCalendarConfig.DefaultHorizonDays,
        sConfigMgr->GetOption<uint32>("RealmCalendar.MaxHorizonDays", 730));
}

struct HolidaySource
{
    uint16 EventId = 0;
    GameEventData const* Event = nullptr;
};

struct CalendarOccurrence
{
    uint16 EventId = 0;
    uint32 HolidayId = 0;
    uint8 HolidayStage = 0;
    std::string Name;
    time_t Start = 0;
    time_t End = 0;
};

std::string FormatLocalTime(time_t value)
{
    if (value <= 0)
        return "n/a";

    std::tm tmValue{};
#if defined(_WIN32)
    localtime_s(&tmValue, &value);
#else
    localtime_r(&value, &tmValue);
#endif

    std::ostringstream out;
    out << std::put_time(&tmValue, "%Y-%m-%d %H:%M");
    return out.str();
}

std::string FormatHolidayDate(uint32 packedDate)
{
    if (!packedDate)
        return "n/a";

    uint32 const packedYear = (packedDate >> 24) & 0x1F;
    std::tm unpacked = HolidayDateCalculator::UnpackDate(packedDate);

    std::ostringstream out;
    if (packedYear == 31)
        out << "yearly ";

    out << std::setfill('0')
        << std::setw(2) << (unpacked.tm_mon + 1) << '-'
        << std::setw(2) << unpacked.tm_mday << ' '
        << std::setw(2) << unpacked.tm_hour << ':'
        << std::setw(2) << unpacked.tm_min;

    if (packedYear != 31)
        out << " (" << (unpacked.tm_year + 1900) << ')';

    return out.str();
}

std::string StateName(GameEventState state)
{
    switch (state)
    {
        case GAMEEVENT_NORMAL:           return "normal";
        case GAMEEVENT_WORLD_INACTIVE:   return "world-inactive";
        case GAMEEVENT_WORLD_CONDITIONS: return "world-conditions";
        case GAMEEVENT_WORLD_NEXTPHASE:  return "world-nextphase";
        case GAMEEVENT_WORLD_FINISHED:   return "world-finished";
        case GAMEEVENT_INTERNAL:         return "internal";
        default:                         return "unknown";
    }
}

bool IsCalendarFacing(GameEventData const& eventData)
{
    return eventData.HolidayId != HOLIDAY_NONE && !eventData.Description.empty();
}

bool Contains(std::string const& value, std::string const& needle)
{
    return value.find(needle) != std::string::npos;
}

std::string CleanDisplayName(std::string name)
{
    auto removeSuffix = [&name](std::string const& suffix)
    {
        if (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
            name.erase(name.size() - suffix.size());
    };

    removeSuffix(" Fishing Pools");

    // Location suffixes are useful for server-side implementation details, but the
    // client calendar presents the logical holiday name.
    std::size_t const paren = name.find(" (");
    if (paren != std::string::npos)
        name.erase(paren);

    return name;
}

int RepresentativeScore(GameEventData const& eventData)
{
    int score = 0;

    if (eventData.HolidayStage > 0)
        score += 30;

    if (!Contains(eventData.Description, " ("))
        score += 10;

    if (Contains(eventData.Description, "Building"))
        score -= 100;
    if (Contains(eventData.Description, "Fishing Pools"))
        score -= 40;
    if (Contains(eventData.Description, "Fireworks"))
        score -= 60;

    // Prefer concise logical names when everything else is equal.
    score -= static_cast<int>(eventData.Description.size() / 12);
    return score;
}

std::map<uint32, HolidaySource> BuildHolidaySources()
{
    std::map<uint32, HolidaySource> result;
    GameEventMgr::GameEventDataMap const& events = sGameEventMgr->GetEventMap();

    // GameEventMgr stores the event entry at its vector index. Use that index as
    // the canonical ID instead of trusting GameEventData::EventId on branches
    // where older loaders may not populate it consistently.
    for (std::size_t index = 1; index < events.size(); ++index)
    {
        GameEventData const& eventData = events[index];
        if (!IsCalendarFacing(eventData))
            continue;

        uint32 const holidayId = static_cast<uint32>(eventData.HolidayId);
        auto itr = result.find(holidayId);
        if (itr == result.end() || RepresentativeScore(eventData) > RepresentativeScore(*itr->second.Event))
            result[holidayId] = { static_cast<uint16>(index), &eventData };
    }

    return result;
}

uint32 StageDurationHours(HolidaysEntry const& holiday, GameEventData const& eventData)
{
    if (eventData.HolidayStage > 0)
    {
        uint8 const stageIndex = eventData.HolidayStage - 1;
        if (stageIndex < MAX_HOLIDAY_DURATIONS && holiday.Duration[stageIndex])
            return holiday.Duration[stageIndex];
    }

    if (holiday.Duration[0])
        return holiday.Duration[0];

    // Fallback for malformed/custom holiday DBC rows.
    return std::max<uint32>(1, (eventData.Length + 59) / 60);
}

time_t StageOffsetSeconds(HolidaysEntry const& holiday, GameEventData const& eventData)
{
    if (eventData.HolidayStage <= 1)
        return 0;

    uint8 const stageIndex = eventData.HolidayStage - 1;
    time_t offset = 0;
    for (uint8 i = 0; i < stageIndex && i < MAX_HOLIDAY_DURATIONS; ++i)
        offset += static_cast<time_t>(holiday.Duration[i]) * HOUR;

    return offset;
}

time_t MakeLocalTimeForYear(uint32 packedDate, int year)
{
    std::tm value{};
    value.tm_year = year - 1900;
    value.tm_mon = static_cast<int>((packedDate >> 20) & 0xF);
    value.tm_mday = static_cast<int>(((packedDate >> 14) & 0x3F) + 1);
    value.tm_hour = static_cast<int>((packedDate >> 6) & 0x1F);
    value.tm_min = static_cast<int>(packedDate & 0x3F);
    value.tm_sec = 0;
    value.tm_isdst = -1;
    return std::mktime(&value);
}

time_t PackedHolidayDateToTime(uint32 packedDate)
{
    std::tm value = HolidayDateCalculator::UnpackDate(packedDate);
    value.tm_isdst = -1;
    return std::mktime(&value);
}

void AppendOccurrence(std::vector<CalendarOccurrence>& output, HolidaySource const& source,
                      std::string const& name, time_t start, time_t end,
                      time_t windowStart, time_t windowEnd)
{
    if (start <= 0 || end <= start)
        return;
    if (end < windowStart || start > windowEnd)
        return;

    output.push_back({ source.EventId,
        static_cast<uint32>(source.Event->HolidayId),
        source.Event->HolidayStage,
        name,
        start,
        end });
}

void AppendHolidayOccurrences(HolidaySource const& source, time_t windowStart, time_t windowEnd,
                              std::vector<CalendarOccurrence>& output)
{
    GameEventData const& eventData = *source.Event;
    HolidaysEntry const* holiday = sHolidaysStore.LookupEntry(eventData.HolidayId);
    if (!holiday || !holiday->Date[0])
        return;

    std::string const name = CleanDisplayName(eventData.Description);
    time_t const stageOffset = StageOffsetSeconds(*holiday, eventData);
    time_t const duration = static_cast<time_t>(StageDurationHours(*holiday, eventData)) * HOUR;

    std::set<time_t> emittedStarts;

    auto emitBaseStart = [&](time_t baseStart)
    {
        time_t const start = baseStart + stageOffset;
        if (!emittedStarts.insert(start).second)
            return;
        AppendOccurrence(output, source, name, start, start + duration, windowStart, windowEnd);
    };

    // Weekly calendar events (fishing contests) and looping holiday rows use the
    // DBC packed date as the phase anchor, then repeat according to the calendar
    // definition. This is the important distinction from expanding support rows
    // such as "Fishing Pools" from game_event.start_time.
    bool const weekly = holiday->CalendarFilterType == 0 && !holiday->Looping;
    if (weekly || holiday->Looping)
    {
        time_t period = WEEK;
        if (holiday->Looping)
        {
            period = 0;
            for (uint8 i = 0; i < MAX_HOLIDAY_DURATIONS && holiday->Duration[i]; ++i)
                period += static_cast<time_t>(holiday->Duration[i]) * HOUR;
        }

        if (period <= 0)
            return;

        time_t anchor = PackedHolidayDateToTime(holiday->Date[0]) + stageOffset;
        if (anchor <= 0)
            return;

        int64 occurrenceIndex = 0;
        if (windowStart > anchor)
            occurrenceIndex = static_cast<int64>((windowStart - anchor) / period);
        if (occurrenceIndex > 0)
            --occurrenceIndex;

        for (;; ++occurrenceIndex)
        {
            time_t const start = anchor + static_cast<time_t>(occurrenceIndex) * period;
            if (start > windowEnd)
                break;
            if (start + duration < windowStart)
                continue;

            if (emittedStarts.insert(start).second)
                AppendOccurrence(output, source, name, start, start + duration, windowStart, windowEnd);
        }
        return;
    }

    for (uint8 i = 0; i < MAX_HOLIDAY_DATES && holiday->Date[i]; ++i)
    {
        uint32 const packedDate = holiday->Date[i];
        uint32 const packedYear = (packedDate >> 24) & 0x1F;

        if (packedYear == 31)
        {
            std::tm windowTm{};
#if defined(_WIN32)
            localtime_s(&windowTm, &windowStart);
#else
            localtime_r(&windowStart, &windowTm);
#endif
            int const currentYear = windowTm.tm_year + 1900;
            for (int year = currentYear - 1; year <= currentYear + 2; ++year)
                emitBaseStart(MakeLocalTimeForYear(packedDate, year));
        }
        else
        {
            emitBaseStart(PackedHolidayDateToTime(packedDate));
        }
    }
}

std::vector<CalendarOccurrence> BuildOccurrences(uint32 horizonDays)
{
    std::vector<CalendarOccurrence> result;
    time_t const now = std::time(nullptr);
    time_t const windowEnd = now + static_cast<time_t>(horizonDays) * DAY;

    for (auto const& [holidayId, source] : BuildHolidaySources())
    {
        (void)holidayId;
        AppendHolidayOccurrences(source, now, windowEnd, result);
    }

    std::sort(result.begin(), result.end(), [](CalendarOccurrence const& left, CalendarOccurrence const& right)
    {
        if (left.Start != right.Start)
            return left.Start < right.Start;
        if (left.HolidayId != right.HolidayId)
            return left.HolidayId < right.HolidayId;
        return left.EventId < right.EventId;
    });

    return result;
}
}

class realm_calendar_configscript : public WorldScript
{
public:
    realm_calendar_configscript() : WorldScript("realm_calendar_configscript", { WORLDHOOK_ON_BEFORE_CONFIG_LOAD }) { }

    void OnBeforeConfigLoad(bool /*reload*/) override
    {
        LoadRealmCalendarConfig();
    }
};

class realm_calendar_commandscript : public CommandScript
{
public:
    realm_calendar_commandscript() : CommandScript("realm_calendar_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable realmCalendarCommandTable =
        {
            { "upcoming", HandleUpcomingCommand, SEC_ADMINISTRATOR, Console::Yes },
            { "holidays", HandleHolidaysCommand, SEC_ADMINISTRATOR, Console::Yes },
            { "inspect",  HandleInspectCommand,  SEC_ADMINISTRATOR, Console::Yes }
        };

        static ChatCommandTable commandTable =
        {
            { "realmcalendar", realmCalendarCommandTable }
        };

        return commandTable;
    }

private:
    static bool HandleUpcomingCommand(ChatHandler* handler, Optional<uint32> horizonDaysArg)
    {
        if (!gRealmCalendarConfig.Enabled)
        {
            handler->SendSysMessage("[Realm Calendar] Module is disabled in mod_realm_calendar.conf.");
            return true;
        }

        uint32 horizonDays = horizonDaysArg.value_or(gRealmCalendarConfig.DefaultHorizonDays);
        horizonDays = std::clamp(horizonDays, uint32(1), gRealmCalendarConfig.MaxHorizonDays);

        std::vector<CalendarOccurrence> const occurrences = BuildOccurrences(horizonDays);
        handler->PSendSysMessage("[Realm Calendar] Logical public calendar occurrences for the next {} day(s):", horizonDays);

        if (occurrences.empty())
        {
            handler->SendSysMessage("[Realm Calendar] No public holiday occurrences were found in that window.");
            return true;
        }

        time_t const now = std::time(nullptr);
        for (CalendarOccurrence const& occurrence : occurrences)
        {
            bool const active = occurrence.Start <= now && occurrence.End > now;
            handler->PSendSysMessage(
                "[{}] event={} holiday={} stage={} {} -> {}{}",
                occurrence.Name,
                occurrence.EventId,
                occurrence.HolidayId,
                uint32(occurrence.HolidayStage),
                FormatLocalTime(occurrence.Start),
                FormatLocalTime(occurrence.End),
                active ? " [ACTIVE]" : "");
        }

        handler->PSendSysMessage("[Realm Calendar] {} logical occurrence(s) found.", occurrences.size());
        return true;
    }

    static bool HandleHolidaysCommand(ChatHandler* handler)
    {
        if (!gRealmCalendarConfig.Enabled)
        {
            handler->SendSysMessage("[Realm Calendar] Module is disabled in mod_realm_calendar.conf.");
            return true;
        }

        auto const sources = BuildHolidaySources();
        handler->PSendSysMessage("[Realm Calendar] {} logical holiday definition(s):", sources.size());

        for (auto const& [holidayId, source] : sources)
        {
            GameEventData const& eventData = *source.Event;
            HolidaysEntry const* holiday = sHolidaysStore.LookupEntry(holidayId);
            if (!holiday)
                continue;

            handler->PSendSysMessage(
                "holiday={} event={} stage={} filter={} looping={} name='{}' texture='{}'",
                holidayId,
                source.EventId,
                uint32(eventData.HolidayStage),
                holiday->CalendarFilterType,
                holiday->Looping,
                CleanDisplayName(eventData.Description),
                holiday->TextureFilename ? holiday->TextureFilename : "");

            std::ostringstream durations;
            bool firstDuration = true;
            for (uint8 i = 0; i < MAX_HOLIDAY_DURATIONS && holiday->Duration[i]; ++i)
            {
                if (!firstDuration)
                    durations << ", ";
                durations << "stage" << uint32(i + 1) << '=' << holiday->Duration[i] << 'h';
                firstDuration = false;
            }
            handler->PSendSysMessage("  durations: {}", durations.str().empty() ? "none" : durations.str());

            std::ostringstream dates;
            bool firstDate = true;
            for (uint8 i = 0; i < MAX_HOLIDAY_DATES && holiday->Date[i]; ++i)
            {
                if (!firstDate)
                    dates << ", ";
                dates << '#' << uint32(i) << ' ' << FormatHolidayDate(holiday->Date[i]);
                firstDate = false;
            }
            handler->PSendSysMessage("  dates: {}", dates.str().empty() ? "none" : dates.str());
        }

        return true;
    }

    static bool HandleInspectCommand(ChatHandler* handler)
    {
        if (!gRealmCalendarConfig.Enabled)
        {
            handler->SendSysMessage("[Realm Calendar] Module is disabled in mod_realm_calendar.conf.");
            return true;
        }

        GameEventMgr::GameEventDataMap const& events = sGameEventMgr->GetEventMap();
        GameEventMgr::ActiveEvents const& activeEvents = sGameEventMgr->GetActiveEventList();

        uint32 count = 0;
        handler->SendSysMessage("[Realm Calendar] Raw calendar-facing GameEventMgr entries:");

        for (std::size_t index = 1; index < events.size(); ++index)
        {
            GameEventData const& eventData = events[index];
            if (!IsCalendarFacing(eventData))
                continue;

            uint16 const eventId = static_cast<uint16>(index);
            bool const active = activeEvents.find(eventId) != activeEvents.end();
            handler->PSendSysMessage(
                "event={} holiday={} stage={} state={} start={} end={} every={}m length={}m{} :: {}",
                eventId,
                static_cast<uint32>(eventData.HolidayId),
                uint32(eventData.HolidayStage),
                StateName(eventData.State),
                FormatLocalTime(eventData.Start),
                FormatLocalTime(eventData.End),
                eventData.Occurence,
                eventData.Length,
                active ? " [ACTIVE]" : "",
                eventData.Description);
            ++count;
        }

        handler->PSendSysMessage("[Realm Calendar] {} raw calendar-facing event definition(s) found.", count);
        return true;
    }
};

void Addmod_realm_calendarScripts()
{
    new realm_calendar_configscript();
    new realm_calendar_commandscript();
}
