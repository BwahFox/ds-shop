#pragma once
#include <stdbool.h>
#include "config.h"

bool wifi_connect(const Config *config);
void wifi_disconnect(void);
