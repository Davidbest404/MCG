#include <catch2/catch_test_macros.hpp>

#include "shared/attributes/attribute_map.h"

using namespace mcg;

TEST_CASE("AttributeMap basic get/set", "[attributes]")
{
    AttributeMap m;
    REQUIRE(m.empty());
    REQUIRE_FALSE(m.has("hp"));
    REQUIRE(m.get("hp").is_null());

    m.set("hp", 100);
    REQUIRE(m.has("hp"));
    REQUIRE(m.get("hp") == 100);
    REQUIRE(m.size() == 1);

    m.set("hp", 50);
    REQUIRE(m.get("hp") == 50);

    m.set("name", "Ivan");
    REQUIRE(m.get("name") == "Ivan");
}

TEST_CASE("AttributeMap erase and clear", "[attributes]")
{
    AttributeMap m;
    m.set("a", 1);
    m.set("b", 2);
    REQUIRE(m.size() == 2);

    REQUIRE(m.erase("a"));
    REQUIRE_FALSE(m.has("a"));
    REQUIRE_FALSE(m.erase("a"));

    m.clear();
    REQUIRE(m.empty());
}

TEST_CASE("AttributeMap JSON round-trip", "[attributes]")
{
    AttributeMap m;
    m.set("hp", 100);
    m.set("name", "Ivan");
    m.set("alive", true);
    m.set("model", nlohmann::json{{"id", "cat"}, {"scale", 1.5}});

    const auto j = m.to_json();
    REQUIRE(j.is_object());
    REQUIRE(j["hp"] == 100);
    REQUIRE(j["name"] == "Ivan");
    REQUIRE(j["alive"] == true);
    REQUIRE(j["model"]["id"] == "cat");

    const auto restored = AttributeMap::from_json(j);
    REQUIRE(restored.size() == m.size());
    REQUIRE(restored.get("hp") == 100);
    REQUIRE(restored.get("name") == "Ivan");
    REQUIRE(restored.get("alive") == true);
    REQUIRE(restored.get("model")["id"] == "cat");
}

TEST_CASE("AttributeMap from_json ignores non-object", "[attributes]")
{
    const auto m1 = AttributeMap::from_json(nlohmann::json::array());
    REQUIRE(m1.empty());

    const auto m2 = AttributeMap::from_json(nlohmann::json());
    REQUIRE(m2.empty());
}