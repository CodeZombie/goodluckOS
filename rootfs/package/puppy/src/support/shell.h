#pragma once

#include <string>

// Quote a single argument for the shell, including embedded apostrophes.
std::string shellQuote(const std::string& s);
