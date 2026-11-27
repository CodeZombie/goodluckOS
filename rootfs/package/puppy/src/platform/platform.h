#pragma once

#include "domain/model.h"

bool readAutoStartId(std::string& category, std::string& name);
void toggleAutoStart(Model& model);
void launch(const Entry& entry);
void restoreCursor(Model& model);
int readBattery();
