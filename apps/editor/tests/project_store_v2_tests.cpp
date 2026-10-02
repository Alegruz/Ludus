// Offscreen tests for version-2 descriptor parsing and serialization. These run
// the C++ ProjectStore reader against the SAME shared fixtures
// (apps/editor/tests/fixtures/cases_v2.json) consumed by the Python v2 reader in
// scripts/python/test_ludus_tools.py, so the C++/Python v2 contracts cannot
// drift (requirement P04/P07; design "Project files and schema evolution").
//
// Version-1 behavior remains covered by project_store_tests.cpp; these cases
// additionally prove the old-reader rule still holds (v1 rejects the Release
// preset) and the new v2 engine/template handling.

#include "internal/project_descriptor.h"
#include "internal/project_store.h"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

using namespace ludus::editor;

namespace
{
QByteArray ReadFixture(const QString& name)
{
    QFile file(QStringLiteral(LUDUS_EDITOR_SOURCE_DIR) + QStringLiteral("/tests/fixtures/") + name);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

ResultCode CodeFromName(const QString& name)
{
    if (name == QStringLiteral("InvalidProject"))
    {
        return ResultCode::InvalidProject;
    }
    if (name == QStringLiteral("UnsupportedVersion"))
    {
        return ResultCode::UnsupportedVersion;
    }
    return ResultCode::Ok;
}
} // namespace

TEST_CASE("Shared v2 fixtures parse with the same verdicts as the Python v2 reader", "[editor][store][v2]")
{
    const QByteArray manifestBytes = ReadFixture(QStringLiteral("cases_v2.json"));
    QJsonParseError error{};
    const QJsonDocument manifest = QJsonDocument::fromJson(manifestBytes, &error);
    REQUIRE(error.error == QJsonParseError::NoError);
    REQUIRE(manifest.isArray());

    for (const QJsonValue& caseValue : manifest.array())
    {
        const QJsonObject testCase = caseValue.toObject();
        const QString fileName = testCase.value(QStringLiteral("file")).toString();
        const QByteArray data = ReadFixture(fileName);
        const ParseOutcome outcome = ParseDescriptor(data);

        if (testCase.value(QStringLiteral("valid")).toBool())
        {
            INFO("expected valid: " << fileName.toStdString());
            REQUIRE(outcome.Ok());
            CHECK(outcome.Descriptor.Version == static_cast<foundation::uint32>(
                                                     testCase.value(QStringLiteral("version")).toInt()));
            CHECK(outcome.Descriptor.Name == testCase.value(QStringLiteral("name")).toString());
            CHECK(outcome.Descriptor.Preset == testCase.value(QStringLiteral("preset")).toString());
            if (testCase.value(QStringLiteral("provider")).toString() == QStringLiteral("cmake") &&
                testCase.value(QStringLiteral("has_engine")).toBool(true))
            {
                REQUIRE(outcome.Descriptor.HasEngine);
                CHECK(outcome.Descriptor.Engine.Version ==
                      testCase.value(QStringLiteral("engine_version")).toString());
            }
            else
            {
                CHECK_FALSE(outcome.Descriptor.HasEngine);
            }
        }
        else
        {
            INFO("expected invalid: " << fileName.toStdString());
            CHECK_FALSE(outcome.Ok());
            CHECK(outcome.Code == CodeFromName(testCase.value(QStringLiteral("code")).toString()));
        }
    }
}

TEST_CASE("Version-2 engine/template round-trip through serialize+reparse", "[editor][store][v2]")
{
    const ParseOutcome parsed = ParseDescriptor(ReadFixture(QStringLiteral("valid_v2_cmake.json")));
    REQUIRE(parsed.Ok());
    REQUIRE(parsed.Descriptor.Version == 2u);
    REQUIRE(parsed.Descriptor.HasEngine);
    REQUIRE(parsed.Descriptor.HasTemplate);
    CHECK(parsed.Descriptor.Engine.Version == QStringLiteral("0.1.0"));
    CHECK(parsed.Descriptor.Engine.Components.size() == 2);
    CHECK(parsed.Descriptor.Template.Id == QStringLiteral("minimal"));
    CHECK(parsed.Descriptor.Template.Version == 1u);
    CHECK(parsed.Descriptor.Preset == QStringLiteral("linux-clang-release"));

    const QByteArray serialized = SerializeDescriptor(parsed.Descriptor);
    const ParseOutcome reparsed = ParseDescriptor(serialized);
    REQUIRE(reparsed.Ok());
    CHECK(reparsed.Descriptor == parsed.Descriptor);
}

TEST_CASE("Version-1 reader still rejects the Release preset (old-reader rule)", "[editor][store][v2]")
{
    const ParseOutcome outcome = ParseDescriptor(ReadFixture(QStringLiteral("invalid_v2_release_in_v1.json")));
    CHECK_FALSE(outcome.Ok());
    CHECK(outcome.Code == ResultCode::InvalidProject);
}
