/* Trimmed copies of real Floppy replies, seen on 5 October 2026. Personal
 * notes and addresses are left out. */
#pragma once

/* IGDB search for "SILENT HILL f", all five results. media_id is a number. */
static const char search_reply[] =
  "{\"pagination\":{\"total\":5,\"limit\":5,\"offset\":0,\"next\":null,"
  "\"previous\":null},\"results\":["
  "{\"media_id\":222343,\"source\":\"igdb\",\"media_type\":\"game\","
  "\"title\":\"Silent Hill f\",\"year\":2025,"
  "\"platforms\":[\"Xbox Series X|S\",\"PC (Microsoft Windows)\","
  "\"PlayStation 5\"]},"
  "{\"media_id\":417146,\"source\":\"igdb\",\"media_type\":\"game\","
  "\"title\":\"Silent Hill f x Urban Myth Dissolution Center\",\"year\":2026,"
  "\"platforms\":[\"Web browser\"]},"
  "{\"media_id\":381094,\"source\":\"igdb\",\"media_type\":\"game\","
  "\"title\":\"Silent Hill f: Steelbook Edition\",\"year\":2025,"
  "\"platforms\":[\"Xbox Series X|S\",\"PlayStation 5\"]},"
  "{\"media_id\":370229,\"source\":\"igdb\",\"media_type\":\"game\","
  "\"title\":\"Silent Hill f: Day One Edition\",\"year\":2025,"
  "\"platforms\":[\"Xbox Series X|S\",\"PlayStation 5\"]},"
  "{\"media_id\":347180,\"source\":\"igdb\",\"media_type\":\"game\","
  "\"title\":\"Silent Hill f: Deluxe Edition\",\"year\":2025,"
  "\"platforms\":[\"Xbox Series X|S\",\"PC (Microsoft Windows)\","
  "\"PlayStation 5\"]}]}";

/* Library search for "SILENT HILL f". media_id is a string inside `item`,
 * `source` appears at two levels, and there are fractions, nulls and empty
 * containers. */
static const char library_reply[] =
  "{\"pagination\":{\"total\":1,\"limit\":20,\"offset\":0,\"next\":null,"
  "\"previous\":null},\"results\":[{\"id\":35614,\"consumption_id\":1058,"
  "\"item\":{\"media_id\":\"347180\",\"ids\":{},\"source\":\"igdb\","
  "\"title\":\"Silent Hill f: Deluxe Edition\",\"original_title\":null,"
  "\"provider_rating\":9.2,\"implied_genres\":[],"
  "\"provider_game_lengths\":{\"igdb\":{\"raw\":[],\"game_id\":347180,"
  "\"summary\":{\"count\":0}},\"active_source\":\"igdb\"},"
  "\"platforms\":[\"Xbox Series X|S\",\"PlayStation 5\"]},"
  "\"item_id\":\"game/igdb/347180\",\"tracked\":true,\"score\":null,"
  "\"status\":1,\"progress\":0,\"progress_unit\":\"minutes\","
  "\"source\":\"PlayStation\",\"lists\":[]}]}";

/* PATCH {"progress": 0} to a real Deluxe Edition entry, 6 October 2026.
 * The detail shape: the user's entries are in `consumptions`. Synopsis
 * trimmed. */
static const char patch_reply[] =
  "{\"id\":35614,\"media_id\":\"347180\",\"source\":\"igdb\","
  "\"media_type\":\"game\",\"title\":\"Silent Hill f: Deluxe Edition\","
  "\"max_progress\":1,\"episodes_left\":null,\"synopsis\":\"It is set in "
  "rural Japan during the 1960s, marking a departure from the franchise\xe2"
  "\x80\x99s traditional Western settings.\\nThe Deluxe Edition...\","
  "\"details\":{\"format\":\"Main game\",\"platforms\":[\"PlayStation 5\"]},"
  "\"related\":{\"dlcs\":[]},\"item_id\":\"game/igdb/347180\","
  "\"tracked\":true,\"consumptions_number\":1,\"consumptions\":["
  "{\"consumption_id\":1058,\"progress\":0,\"status\":1,"
  "\"start_date\":\"2026-09-30T23:16:16Z\",\"source\":\"PlayStation\"}],"
  "\"lists\":[],\"media_type_status\":null}";

/* param.json of two installed games, fetched from a real console on
 * 6 October 2026, trimmed to the title ID and the names. Languages are in
 * the file's own order: alphabetical, with defaultLanguage among them. */
static const char wolverine_param[] =
  "{\"titleId\": \"PPSA03671\", \"localizedParameters\": {\"ar-AE\": {\"t"
  "itleName\": \"\xd9\x88\xd9\x88\xd9\x84\xda\xa4\xd8\xb1\xd9\x8a\xd9\x86"
  " \xd9\x85\xd9\x86 \xd9\x85\xd8\xa7\xd8\xb1\xd9\x81\xd9\x84\"}, \"defau"
  "ltLanguage\": \"en-US\", \"en-US\": {\"titleName\": \"Marvel's Wolveri"
  "ne\"}, \"es-ES\": {\"titleName\": \"Marvel: Lobezno\"}, \"ru-RU\": {\""
  "titleName\": \"Marvel: \xd0\xa0\xd0\xbe\xd1\x81\xd0\xbe\xd0\xbc\xd0"
  "\xb0\xd1\x85\xd0\xb0\"}, \"th-TH\": {\"titleName\": \"\xe0\xb8\xa1\xe0"
  "\xb8\xb2\xe0\xb8\xa3\xe0\xb9\x8c\xe0\xb9\x80\xe0\xb8\xa7\xe0\xb8\xa5 "
  "\xe0\xb8\xa7\xe0\xb8\xb9\xe0\xb8\xa5\xe0\xb9\x8c\xe0\xb8\x9f\xe0\xb9"
  "\x80\xe0\xb8\xa7\xe0\xb8\xad\xe0\xb8\xa3\xe0\xb8\xb5\xe0\xb8\x99\"}, "
  "\"zh-Hans\": {\"titleName\": \"\xe6\xbc\xab\xe5\xa8\x81\xe9\x87\x91"
  "\xe5\x88\x9a\xe7\x8b\xbc\"}, \"zh-Hant\": {\"titleName\": \"\xe6\xbc"
  "\xab\xe5\xa8\x81\xe9\x87\x91\xe9\x8b\xbc\xe7\x8b\xbc\"}}}";
static const char silenthill_param[] =
  "{\"titleId\": \"PPSA21159\", \"localizedParameters\": {\"de-DE\": {\"t"
  "itleName\": \"SILENT HILL f\"}, \"defaultLanguage\": \"en-US\", \"en-U"
  "S\": {\"titleName\": \"SILENT HILL f\"}, \"es-419\": {\"titleName\": "
  "\"SILENT HILL f\"}, \"es-ES\": {\"titleName\": \"SILENT HILL f\"}, \"f"
  "r-FR\": {\"titleName\": \"SILENT HILL f\"}, \"it-IT\": {\"titleName\":"
  " \"SILENT HILL f\"}, \"ja-JP\": {\"titleName\": \"SILENT HILL f\"}, \""
  "ko-KR\": {\"titleName\": \"SILENT HILL f\"}, \"pl-PL\": {\"titleName\""
  ": \"SILENT HILL f\"}, \"pt-BR\": {\"titleName\": \"SILENT HILL f\"}, "
  "\"ru-RU\": {\"titleName\": \"SILENT HILL f\"}, \"zh-Hans\": {\"titleNa"
  "me\": \"SILENT HILL f\"}, \"zh-Hant\": {\"titleName\": \"SILENT HILL f"
  "\"}}}";
