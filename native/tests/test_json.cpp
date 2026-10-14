#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

TEST_CASE("nlohmann/json parses a fixture-shaped document") {
    const auto doc = nlohmann::json::parse(R"({"name": "probe", "values": [1, 2.5, -0.0], "ok": true})");
    CHECK(doc["name"] == "probe");
    CHECK(doc["values"].size() == 3);
    CHECK(doc["values"][1].get<double>() == 2.5);
    CHECK(doc["ok"].get<bool>());
}
