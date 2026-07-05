#ifndef __WEATHER_PARSER_H
#define __WEATHER_PARSER_H

#include "app_types.h"

int Weather_ParseNowResponse(const char *response, WeatherNow *out);
int Weather_ParseDailyResponse(const char *response, ForecastDay out[3]);

#endif /* __WEATHER_PARSER_H */
