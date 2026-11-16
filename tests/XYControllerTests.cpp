#include "plugin/XYController.h"

#include <array>
#include <cmath>
#include <iostream>

namespace
{
int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void expectNear(float actual, float expected, const char* message)
{
    expect(std::abs(actual - expected) < 0.0001f, message);
}
}

int main()
{
    using hypercolor::ui::XYCoordinates;
    using hypercolor::ui::projectToUnitCircle;

    const auto centre = projectToUnitCircle({ 0.5f, 0.5f });
    expectNear(centre.x, 0.5f, "centre X");
    expectNear(centre.y, 0.5f, "centre Y");

    const auto directions = std::array<XYCoordinates, 4> {
        projectToUnitCircle({ 1.0f, 0.5f }),
        projectToUnitCircle({ 0.0f, 0.5f }),
        projectToUnitCircle({ 0.5f, 1.0f }),
        projectToUnitCircle({ 0.5f, 0.0f })
    };
    expectNear(directions[0].x, 1.0f, "right boundary");
    expectNear(directions[1].x, 0.0f, "left boundary");
    expectNear(directions[2].y, 1.0f, "top boundary");
    expectNear(directions[3].y, 0.0f, "bottom boundary");

    const auto corner = projectToUnitCircle({ 1.0f, 1.0f });
    expectNear(corner.x, 0.5f + 0.5f / std::sqrt(2.0f), "corner radial X");
    expectNear(corner.y, 0.5f + 0.5f / std::sqrt(2.0f), "corner radial Y");

    const auto negative = projectToUnitCircle({ -10.0f, 10.0f });
    expect(negative.x >= 0.0f && negative.x <= 1.0f, "projected X bounded");
    expect(negative.y >= 0.0f && negative.y <= 1.0f, "projected Y bounded");

    const auto invalid = projectToUnitCircle({ NAN, 0.0f });
    expectNear(invalid.x, 0.5f, "invalid X falls back to centre");
    expectNear(invalid.y, 0.5f, "invalid Y falls back to centre");

    std::cout << (failures == 0 ? "All XYController tests passed.\n" : "XYController tests failed.\n");
    return failures == 0 ? 0 : 1;
}
