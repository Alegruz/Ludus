#include "internal/project_creation_dialog.h"
#include "internal/project_setup_dialog.h"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>

using namespace ludus::editor;

TEST_CASE("New Project uses a parent picker and protects existing destinations", "[editor][setup]")
{
    QTemporaryDir directory;
    ProjectCreationDialog dialog;
    dialog.show();
    auto* name = dialog.findChild<QLineEdit*>(QStringLiteral("newProjectName"));
    auto* location = dialog.findChild<QLineEdit*>(QStringLiteral("newProjectLocation"));
    auto* sdk = dialog.findChild<QLineEdit*>(QStringLiteral("newProjectSdk"));
    auto* browse = dialog.findChild<QPushButton*>(QStringLiteral("browseProjectLocation"));
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    REQUIRE(name != nullptr);
    REQUIRE(location != nullptr);
    REQUIRE(sdk != nullptr);
    REQUIRE(browse != nullptr);
    REQUIRE(buttons != nullptr);
    CHECK_FALSE(sdk->isVisible());
    location->setText(directory.path());
    name->setText(QStringLiteral("My Game"));
    CHECK(buttons->button(QDialogButtonBox::Ok)->isEnabled());
    CHECK(dialog.Options().Destination == directory.filePath(QStringLiteral("My Game")));
    CHECK(dialog.Options().Sdk.isEmpty());
    CHECK_FALSE(dialog.Options().PrepareEngine);
    REQUIRE(QDir(directory.path()).mkdir(QStringLiteral("My Game")));
    name->setText(QStringLiteral("Other"));
    name->setText(QStringLiteral("My Game"));
    CHECK_FALSE(buttons->button(QDialogButtonBox::Ok)->isEnabled());
    for (const auto& invalid : {QString(), QStringLiteral("../escape"), QStringLiteral("."), QStringLiteral("..")})
    {
        name->setText(invalid);
        CHECK_FALSE(buttons->button(QDialogButtonBox::Ok)->isEnabled());
    }
    name->setText(QStringLiteral("New"));
    location->setText(QStringLiteral("relative"));
    CHECK_FALSE(buttons->button(QDialogButtonBox::Ok)->isEnabled());
    browse->click();
    QApplication::processEvents();
    auto* picker = dialog.findChild<QFileDialog*>();
    REQUIRE(picker != nullptr);
    CHECK(picker->fileMode() == QFileDialog::Directory);
    picker->reject();
    CHECK(location->text() == QStringLiteral("relative"));
    dialog.reject();
    CHECK(QDir(directory.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot) ==
          QStringList{QStringLiteral("My Game")});
}

TEST_CASE("Project repair reuses the selected engine without asking for paths", "[editor][setup]")
{
    QTemporaryDir project;
    ProjectSetupDialog dialog(project.path());
    dialog.show();
    auto* engine = dialog.findChild<QComboBox*>(QStringLiteral("setupEngineChoice"));
    auto* native = dialog.findChild<QLineEdit*>(QStringLiteral("setupSdk"));
    auto* web = dialog.findChild<QLineEdit*>(QStringLiteral("setupWebSdk"));
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    REQUIRE(engine != nullptr);
    REQUIRE(native != nullptr);
    REQUIRE(web != nullptr);
    REQUIRE(buttons != nullptr);
    CHECK_FALSE(native->isVisible());
    CHECK_FALSE(web->isVisible());
    CHECK(buttons->button(QDialogButtonBox::Ok)->isEnabled());
    CHECK(dialog.Options().Sdk.isEmpty());
    CHECK_FALSE(dialog.Options().PrepareEngine);
    CHECK(dialog.Options().DisableWeb);
    CHECK(QDir(project.path()).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());

    engine->setCurrentIndex(2);
    CHECK(native->isVisible());
    CHECK_FALSE(buttons->button(QDialogButtonBox::Ok)->isEnabled());
    native->setText(QStringLiteral(" /installed/desktop "));
    CHECK(buttons->button(QDialogButtonBox::Ok)->isEnabled());
    CHECK(dialog.Options().Sdk == QStringLiteral("/installed/desktop"));
    engine->setCurrentIndex(1);
    CHECK_FALSE(native->isVisible());
    CHECK(dialog.Options().Sdk.isEmpty());
    CHECK(dialog.Options().PrepareEngine);
    engine->setCurrentIndex(0);
    CHECK(dialog.Options().Sdk.isEmpty());
    CHECK_FALSE(dialog.Options().PrepareEngine);
}

TEST_CASE("Browser repair needs a browser engine only when enabled", "[editor][setup]")
{
    QTemporaryDir project;
    ProjectSetupDialog dialog(project.path());
    dialog.show();
    auto* browser = dialog.findChild<QCheckBox*>(QStringLiteral("setupBrowser"));
    auto* web = dialog.findChild<QLineEdit*>(QStringLiteral("setupWebSdk"));
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    REQUIRE(browser != nullptr);
    REQUIRE(web != nullptr);
    REQUIRE(buttons != nullptr);
    browser->setChecked(true);
    CHECK(web->isVisible());
    CHECK_FALSE(buttons->button(QDialogButtonBox::Ok)->isEnabled());
    web->setText(QStringLiteral(" /installed/browser "));
    CHECK(buttons->button(QDialogButtonBox::Ok)->isEnabled());
    CHECK(dialog.Options().WebSdk == QStringLiteral("/installed/browser"));
    CHECK_FALSE(dialog.Options().DisableWeb);
    browser->setChecked(false);
    CHECK_FALSE(web->isVisible());
    CHECK(buttons->button(QDialogButtonBox::Ok)->isEnabled());
    CHECK(dialog.Options().WebSdk.isEmpty());
    CHECK(dialog.Options().DisableWeb);
}

TEST_CASE("Project repair displays saved browser setup and can turn it off", "[editor][setup]")
{
    QTemporaryDir project;
    REQUIRE(QDir(project.path()).mkdir(QStringLiteral(".ludus")));
    QFile saved(project.filePath(QStringLiteral(".ludus/setup.json")));
    REQUIRE(saved.open(QIODevice::WriteOnly));
    const QByteArray metadata = QByteArrayLiteral("{\"web_sdk\":\"/old/browser-engine\",\"signature\":\"previous\"}");
    REQUIRE(saved.write(metadata) == metadata.size());
    saved.close();
    ProjectSetupDialog dialog(project.path());
    dialog.show();
    auto* browser = dialog.findChild<QCheckBox*>(QStringLiteral("setupBrowser"));
    auto* web = dialog.findChild<QLineEdit*>(QStringLiteral("setupWebSdk"));
    REQUIRE(browser != nullptr);
    REQUIRE(web != nullptr);
    CHECK(browser->isChecked());
    CHECK(web->isVisible());
    CHECK(dialog.Options().WebSdk == QStringLiteral("/old/browser-engine"));
    browser->setChecked(false);
    CHECK(dialog.Options().DisableWeb);
    REQUIRE(saved.open(QIODevice::ReadOnly));
    CHECK(saved.readAll() == metadata);
}
