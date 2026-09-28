#pragma once
#include <stdbool.h>
#include "config.h"

bool wifi_connect(const Config *config);
void wifi_disconnect(void);
unsigned wifi_get_signal_strength(void);
