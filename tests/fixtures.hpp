// Wczytywanie fixtures — testy NIE dotykaja sieci (§9).
#pragma once

#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <string>

inline nlohmann::json load_fixture(const std::string& name) {
    const std::string path = std::string(PREDEYE_FIXTURES_DIR) + "/" + name;
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("brak fixture: " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    return nlohmann::json::parse(ss.str());
}
