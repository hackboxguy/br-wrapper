// The System image section's rolled-back line, from engine status files as
// the device publishes them. Host only: QtCore, no window, no device.
//   cmake -DBUILD_TESTS=ON .. && make test_outcome_text && ctest
#include "SystemImageText.h"

#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

static int failures = 0;

static void expect(const QString &label, const QString &got, const QString &want)
{
    if (got == want) {
        std::printf("  ok  %s\n", qPrintable(label));
    } else {
        std::printf("FAIL: %s\n  got:  %s\n  want: %s\n", qPrintable(label), qPrintable(got), qPrintable(want));
        ++failures;
    }
}

static QString writeStatus(const QTemporaryDir &dir, const QString &name, const QByteArray &body)
{
    const QString path = dir.filePath(name);
    QFile f(path);
    f.open(QIODevice::WriteOnly | QIODevice::Truncate);
    f.write(body);
    return path;
}

// What SystemImageController::lastOutcomeText does on state=fallback
static QString rolledBackLine(const QString &statusPath, const QString &running)
{
    using namespace SystemImageText;
    if (fileValue(statusPath, "state") != "fallback") return QString();
    return fallbackText(fileValue(statusPath, "version"), running, fileValue(statusPath, "refused_reason"));
}

int main()
{
    QTemporaryDir dir;
    if (!dir.isValid()) { std::printf("FAIL: no temporary directory\n"); return 1; }

    // A candidate the commit service refused (engine 2.06+)
    const QString refused = writeStatus(dir, "refused",
        "state=fallback\nversion=2.07\ncandidate_slot=B\n"
        "refused_reason=health lost in the settle window: health unit qt-demo-launcher.service is not active\n");
    expect("fallback with refused_reason ends with the reason", rolledBackLine(refused, "2.06"),
           "The update to 2.07 did not pass its health check; running 2.06 again. "
           "Reason: health lost in the settle window: health unit qt-demo-launcher.service is not active");

    // A candidate that hung or lost power never reached the service: no reason
    const QString noReason = writeStatus(dir, "no-reason", "state=fallback\nversion=2.07\ncandidate_slot=B\n");
    expect("fallback without refused_reason is the line as before", rolledBackLine(noReason, "2.06"),
           "The update to 2.07 did not pass its health check; running 2.06 again");

    // An empty or blank value is no reason either
    const QString blank = writeStatus(dir, "blank", "state=fallback\nversion=2.07\nrefused_reason=   \n");
    expect("blank refused_reason adds nothing", rolledBackLine(blank, "2.06"),
           "The update to 2.07 did not pass its health check; running 2.06 again");

    // The reader: the key is found wherever it is, unknown keys are ignored,
    // and a longer key sharing the prefix is not mistaken for it
    const QString order = writeStatus(dir, "order",
        "refused_reason_extra=no\nstate=fallback\nfuture_key=1\nrefused_reason=why\nversion=2.07\n");
    expect("fileValue matches the exact key", SystemImageText::fileValue(order, "refused_reason"), "why");
    expect("fileValue of an absent file is empty", SystemImageText::fileValue(dir.filePath("absent"), "state"), "");

    if (failures) { std::printf("outcome-text: %d failure(s)\n", failures); return 1; }
    std::printf("outcome-text: PASS\n");
    return 0;
}
