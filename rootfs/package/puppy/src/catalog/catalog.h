#pragma once

#include "domain/model.h"

std::vector<Category> loadCatalog();
// Explicit sources also allow catalogue checks without console filesystem paths.
std::vector<Category> loadCatalog(const std::vector<std::string>& files,
                                  const std::vector<std::string>& directories);
