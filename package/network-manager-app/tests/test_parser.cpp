// NetTool's line parser: net-ctl.sh's RESULT / PROGRESS / NOTICE lines as the
// app reads them. Host only: QtCore, no window, no network.
//   cmake -DBUILD_TESTS=ON .. && make test_parser && ctest
#include "NetTool.h"

#include <QCoreApplication>
#include <cstdio>

static int failures = 0;

static void expect(const QString &label, const QString &got, const QString &want)
{
    if (got == want) {
        std::printf("  ok  %s\n", qPrintable(label));
    } else {
        std::printf("FAIL: %s\n  got:  [%s]\n  want: [%s]\n", qPrintable(label), qPrintable(got), qPrintable(want));
        ++failures;
    }
}

static void expectTrue(const QString &label, bool ok)
{
    expect(label, ok ? "true" : "false", "true");
}

static QString field(const QString &line, const QString &key)
{
    return NetTool::parseLine(line).fields.value(key).toString();
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // SSIDs as net-ctl.sh encodes them: a space, "=", "%", a quote, non-ASCII,
    // and a colon (left as it is: MACs and IPv6 addresses carry them)
    expect("space", field("RESULT kind=ap ssid=El%20Duel signal=47", "ssid"), "El Duel");
    expect("equals sign", field("RESULT kind=ap ssid=a%3Db signal=1", "ssid"), "a=b");
    expect("percent sign", field("RESULT kind=ap ssid=100%25%20sure", "ssid"), "100% sure");
    expect("quote", field("RESULT kind=ap ssid=Caf%C3%A9%20%22Am%20Markt%22", "ssid"),
           QString::fromUtf8("Café \"Am Markt\""));
    expect("non-ASCII", field("RESULT kind=ap ssid=%E2%98%95%F0%9F%93%B6", "ssid"),
           QString::fromUtf8("☕\U0001F4F6"));
    expect("colon kept", field("RESULT kind=ap ssid=softliQ:MC_ab0f32", "ssid"), "softliQ:MC_ab0f32");
    expect("MAC", field("RESULT kind=iface mac=2C:CF:67:4F:66:CA", "mac"), "2C:CF:67:4F:66:CA");
    expect("IPv6 list", field("RESULT kind=iface ip6=fdac:d87e::538/128,fe80::1/64 x=1", "ip6"),
           "fdac:d87e::538/128,fe80::1/64");

    // "=" inside a value: only the first "=" splits key from value
    expect("raw = in value", field("RESULT kind=x detail=a=b=c next=1", "detail"), "a=b=c");
    expect("value after it", field("RESULT kind=x detail=a=b=c next=1", "next"), "1");

    // Empty fields are present and empty
    const NetTool::Line empty = NetTool::parseLine("RESULT kind=iface name=wlan0 ssid= signal= band=5");
    expectTrue("empty field present", empty.fields.contains("ssid"));
    expect("empty field empty", empty.fields.value("ssid").toString(), "");
    expect("field after empty ones", empty.fields.value("band").toString(), "5");

    // reason= is free text to the end of the line, taken as it stands
    const NetTool::Line why = NetTool::parseLine("RESULT kind=available ok=0 reason=NetworkManager is not running");
    expect("reason to the end", why.fields.value("reason").toString(), "NetworkManager is not running");
    expect("field before reason", why.fields.value("ok").toString(), "0");
    expect("reason keeps % and =", field("RESULT kind=error ok=0 reason=50% done a=b", "reason"), "50% done a=b");
    expect("reason code", field("RESULT kind=connect ssid=X ok=0 restored=Workshop reason=bad-password", "reason"),
           "bad-password");
    expect("field before reason code", field("RESULT kind=connect ssid=X ok=0 restored=Workshop reason=bad-password",
                                             "restored"), "Workshop");

    // Kinds of line, extra spaces, colour escapes, a malformed percent
    expectTrue("RESULT kind", NetTool::parseLine("RESULT kind=ap").kind == NetTool::Line::Result);
    const NetTool::Line progress = NetTool::parseLine("PROGRESS phase=authenticating");
    expectTrue("PROGRESS kind", progress.kind == NetTool::Line::Progress);
    expect("PROGRESS phase", progress.fields.value("phase").toString(), "authenticating");
    const NetTool::Line notice = NetTool::parseLine("NOTICE changed");
    expectTrue("NOTICE kind", notice.kind == NetTool::Line::Notice);
    expect("NOTICE text", notice.text, "changed");
    expectTrue("other line", NetTool::parseLine("Error: something").kind == NetTool::Line::Other);
    expect("double spaces", field("RESULT  kind=ap   ssid=x  ", "ssid"), "x");
    expect("ANSI escapes dropped", field("\x1B[32mRESULT kind=ap ssid=x\x1B[0m", "ssid"), "x");
    expect("token without =", field("RESULT kind=ap junk ssid=y", "ssid"), "y");
    expect("lone %", field("RESULT kind=ap ssid=50%", "ssid"), "50%");

    // The encoder matches the script's set, and round-trips
    expect("encode space = % quote", NetTool::percentEncode(QString::fromUtf8("a b=c%d\"e")), "a%20b%3Dc%25d%22e");
    expect("encode keeps : / , @ + -", NetTool::percentEncode("a:b/c,d@e+f-g_h.i~j"), "a:b/c,d@e+f-g_h.i~j");
    const QString odd = QString::fromUtf8("Café \"Am Markt\" = 100%");
    expect("round trip", NetTool::percentDecode(NetTool::percentEncode(odd)), odd);

    if (failures) { std::printf("parser: %d failure(s)\n", failures); return 1; }
    std::printf("parser: PASS\n");
    return 0;
}
