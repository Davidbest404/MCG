#include <catch2/catch_test_macros.hpp>

#include "shared/color/tag_parser.h"

using namespace mcg::color;

TEST_CASE("Plain text without tags", "[color]")
{
    const auto r = parse_colored("Hello, world");
    REQUIRE_FALSE(r.had_errors);
    REQUIRE(r.segments.size() == 1);
    REQUIRE(r.segments[0].text == "Hello, world");
    REQUIRE(r.segments[0].fg_color == -1);
    REQUIRE(r.segments[0].bg_color == -1);
}

TEST_CASE("Single foreground tag", "[color]")
{
    const auto r = parse_colored("Hello [cF]world[/c]!");
    REQUIRE_FALSE(r.had_errors);
    REQUIRE(r.segments.size() == 3);
    REQUIRE(r.segments[0].text == "Hello ");
    REQUIRE(r.segments[0].fg_color == -1);
    REQUIRE(r.segments[1].text == "world");
    REQUIRE(r.segments[1].fg_color == 15);
    REQUIRE(r.segments[2].text == "!");
    REQUIRE(r.segments[2].fg_color == -1);
}

TEST_CASE("Foreground and background", "[color]")
{
    const auto r = parse_colored("[c1][bgF]test[/bg][/c]");
    REQUIRE_FALSE(r.had_errors);
    REQUIRE(r.segments.size() == 1);
    REQUIRE(r.segments[0].text == "test");
    REQUIRE(r.segments[0].fg_color == 1);
    REQUIRE(r.segments[0].bg_color == 15);
}

TEST_CASE("Nested colors restore previous", "[color]")
{
    const auto r = parse_colored("[cF]a[c1]b[/c]c[/c]");
    REQUIRE_FALSE(r.had_errors);
    REQUIRE(r.segments.size() == 3);
    REQUIRE(r.segments[0].text == "a");
    REQUIRE(r.segments[0].fg_color == 15);
    REQUIRE(r.segments[1].text == "b");
    REQUIRE(r.segments[1].fg_color == 1);
    REQUIRE(r.segments[2].text == "c");
    REQUIRE(r.segments[2].fg_color == 15);
}

TEST_CASE("Unmatched close reports error", "[color]")
{
    const auto r = parse_colored("a[/c]b");
    REQUIRE(r.had_errors);
    REQUIRE(r.segments.size() == 2);
    REQUIRE(r.segments[0].text == "a");
    REQUIRE(r.segments[1].text == "b");
}

TEST_CASE("Unclosed tag reports error", "[color]")
{
    const auto r = parse_colored("[cF]open");
    REQUIRE(r.had_errors);
    REQUIRE(r.segments.size() == 1);
    REQUIRE(r.segments[0].text == "open");
    REQUIRE(r.segments[0].fg_color == 15);
}

TEST_CASE("Round-trip via to_tagged_string", "[color]")
{
    const std::string original = "Hello [cF]world[/c]!";
    const auto r1 = parse_colored(original);
    const std::string rebuilt = to_tagged_string(r1);
    const auto r2 = parse_colored(rebuilt);

    REQUIRE(r1.segments.size() == r2.segments.size());
    for (std::size_t i = 0; i < r1.segments.size(); ++i)
    {
        REQUIRE(r1.segments[i].text == r2.segments[i].text);
        REQUIRE(r1.segments[i].fg_color == r2.segments[i].fg_color);
        REQUIRE(r1.segments[i].bg_color == r2.segments[i].bg_color);
    }
}

TEST_CASE("Non-tag brackets are literal", "[color]")
{
    const auto r = parse_colored("array[0] and [x]");
    REQUIRE_FALSE(r.had_errors);
    REQUIRE(r.segments.size() == 1);
    REQUIRE(r.segments[0].text == "array[0] and [x]");
}