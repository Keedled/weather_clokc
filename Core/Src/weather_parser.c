#include "weather_parser.h"

#include <stdio.h>
#include <string.h>

#include "app_config_private.h"

static void CopyString(char *dst, size_t dst_size, const char *src)
{
  if (dst == NULL || dst_size == 0)
  {
    return;
  }

  if (src == NULL)
  {
    src = "";
  }

  snprintf(dst, dst_size, "%s", src);
}

static int ExtractString(const char *src,
                         const char *begin,
                         const char *end,
                         char *out,
                         size_t out_size)
{
  const char *p1;
  const char *p2;
  size_t len;

  if (src == NULL || begin == NULL || end == NULL || out == NULL || out_size == 0)
  {
    return 0;
  }

  p1 = strstr(src, begin);
  if (p1 == NULL)
  {
    return 0;
  }

  p1 += strlen(begin);

  p2 = strstr(p1, end);
  if (p2 == NULL)
  {
    return 0;
  }

  len = (size_t)(p2 - p1);
  if (len >= out_size)
  {
    len = out_size - 1;
  }

  memcpy(out, p1, len);
  out[len] = '\0';

  return 1;
}

static int ExtractStringRange(const char *start,
                              const char *end_limit,
                              const char *begin,
                              const char *end,
                              char *out,
                              size_t out_size)
{
  const char *p1;
  const char *p2;
  size_t len;

  if (start == NULL || end_limit == NULL || begin == NULL || end == NULL || out == NULL || out_size == 0)
  {
    return 0;
  }

  p1 = strstr(start, begin);
  if (p1 == NULL || p1 >= end_limit)
  {
    return 0;
  }

  p1 += strlen(begin);

  p2 = strstr(p1, end);
  if (p2 == NULL || p2 > end_limit)
  {
    return 0;
  }

  len = (size_t)(p2 - p1);
  if (len >= out_size)
  {
    len = out_size - 1;
  }

  memcpy(out, p1, len);
  out[len] = '\0';

  return 1;
}

static const char *FindJsonObjectEnd(const char *obj_start)
{
  const char *p;
  int depth = 0;
  int in_string = 0;
  int escaped = 0;
  char ch;

  if (obj_start == NULL || *obj_start != '{')
  {
    return NULL;
  }

  for (p = obj_start; *p != '\0'; p++)
  {
    ch = *p;

    if (in_string)
    {
      if (escaped)
      {
        escaped = 0;
      }
      else if (ch == '\\')
      {
        escaped = 1;
      }
      else if (ch == '"')
      {
        in_string = 0;
      }
      continue;
    }

    if (ch == '"')
    {
      in_string = 1;
    }
    else if (ch == '{')
    {
      depth++;
    }
    else if (ch == '}')
    {
      depth--;
      if (depth == 0)
      {
        return p;
      }
      if (depth < 0)
      {
        return NULL;
      }
    }
  }

  return NULL;
}

int Weather_ParseNowResponse(const char *response, WeatherNow *out)
{
  char temp_num[8];
  char humidity_num[8];
  int has_weather;
  int has_temperature;

  if (response == NULL || out == NULL)
  {
    return 0;
  }

  memset(out, 0, sizeof(*out));
  CopyString(out->city, sizeof(out->city), WEATHER_CITY);
  CopyString(out->weather, sizeof(out->weather), "--");
  CopyString(out->temperature, sizeof(out->temperature), "--C");
  CopyString(out->humidity, sizeof(out->humidity), "--%");
  CopyString(out->wind_dir, sizeof(out->wind_dir), "--");
  CopyString(out->wind_scale, sizeof(out->wind_scale), "--");
  CopyString(out->update_time, sizeof(out->update_time), "--");

  ExtractString(response,
                "\"name\":\"",
                "\"",
                out->city,
                sizeof(out->city));

  has_weather = ExtractString(response,
                              "\"text\":\"",
                              "\"",
                              out->weather,
                              sizeof(out->weather));

  has_temperature = ExtractString(response,
                                  "\"temperature\":\"",
                                  "\"",
                                  temp_num,
                                  sizeof(temp_num));
  if (!has_temperature)
  {
    has_temperature = ExtractString(response,
                                    "\"temp\":\"",
                                    "\"",
                                    temp_num,
                                    sizeof(temp_num));
  }

  if (has_temperature)
  {
    snprintf(out->temperature,
             sizeof(out->temperature),
             "%sC",
             temp_num);
  }

  if (ExtractString(response,
                    "\"humidity\":\"",
                    "\"",
                    humidity_num,
                    sizeof(humidity_num)))
  {
    snprintf(out->humidity, sizeof(out->humidity), "%s%%", humidity_num);
  }

  if (!ExtractString(response,
                     "\"wind_direction\":\"",
                     "\"",
                     out->wind_dir,
                     sizeof(out->wind_dir)))
  {
    ExtractString(response,
                  "\"windDir\":\"",
                  "\"",
                  out->wind_dir,
                  sizeof(out->wind_dir));
  }

  if (!ExtractString(response,
                     "\"wind_scale\":\"",
                     "\"",
                     out->wind_scale,
                     sizeof(out->wind_scale)))
  {
    ExtractString(response,
                  "\"windScale\":\"",
                  "\"",
                  out->wind_scale,
                  sizeof(out->wind_scale));
  }

  if (!ExtractString(response,
                     "\"last_update\":\"",
                     "\"",
                     out->update_time,
                     sizeof(out->update_time)))
  {
    ExtractString(response,
                  "\"obsTime\":\"",
                  "\"",
                  out->update_time,
                  sizeof(out->update_time));
  }

  if (!has_weather || !has_temperature)
  {
    return 0;
  }

  out->valid = 1;
  return 1;
}

int Weather_ParseDailyResponse(const char *response, ForecastDay out[3])
{
  const char *p;
  const char *obj_start;
  const char *obj_end;
  int i;

  if (response == NULL || out == NULL)
  {
    return 0;
  }

  p = strstr(response, "\"daily\":[");
  if (p == NULL)
  {
    return 0;
  }

  for (i = 0; i < 3; i++)
  {
    obj_start = strstr(p, "{");
    if (obj_start == NULL)
    {
      return 0;
    }

    obj_end = FindJsonObjectEnd(obj_start);
    if (obj_end == NULL)
    {
      return 0;
    }

    memset(&out[i], 0, sizeof(out[i]));
    CopyString(out[i].date, sizeof(out[i].date), "--");
    CopyString(out[i].text_day, sizeof(out[i].text_day), "--");
    CopyString(out[i].text_night, sizeof(out[i].text_night), "--");
    CopyString(out[i].high, sizeof(out[i].high), "--");
    CopyString(out[i].low, sizeof(out[i].low), "--");
    CopyString(out[i].wind_direction, sizeof(out[i].wind_direction), "--");
    CopyString(out[i].wind_scale, sizeof(out[i].wind_scale), "--");

    ExtractStringRange(obj_start, obj_end,
                       "\"date\":\"",
                       "\"",
                       out[i].date,
                       sizeof(out[i].date));

    ExtractStringRange(obj_start, obj_end,
                       "\"text_day\":\"",
                       "\"",
                       out[i].text_day,
                       sizeof(out[i].text_day));

    ExtractStringRange(obj_start, obj_end,
                       "\"text_night\":\"",
                       "\"",
                       out[i].text_night,
                       sizeof(out[i].text_night));

    ExtractStringRange(obj_start, obj_end,
                       "\"high\":\"",
                       "\"",
                       out[i].high,
                       sizeof(out[i].high));

    ExtractStringRange(obj_start, obj_end,
                       "\"low\":\"",
                       "\"",
                       out[i].low,
                       sizeof(out[i].low));

    ExtractStringRange(obj_start, obj_end,
                       "\"wind_direction\":\"",
                       "\"",
                       out[i].wind_direction,
                       sizeof(out[i].wind_direction));

    ExtractStringRange(obj_start, obj_end,
                       "\"wind_scale\":\"",
                       "\"",
                       out[i].wind_scale,
                       sizeof(out[i].wind_scale));

    if (strcmp(out[i].date, "--") == 0 ||
        strcmp(out[i].text_day, "--") == 0 ||
        strcmp(out[i].high, "--") == 0 ||
        strcmp(out[i].low, "--") == 0)
    {
      return 0;
    }

    p = obj_end + 1;
  }

  return 1;
}
