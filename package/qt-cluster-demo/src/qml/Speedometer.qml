import QtQuick 2.12

NeedleGauge {
    value: cluster.speed
    minValue: 0
    maxValue: 260
    startAngle: -135
    endAngle: 135
    majorTickInterval: 20
    minorTickInterval: 10
    labelDivisor: 1
    label: "km/h"
    showDigitalValue: true
    redlineValue: -1
}
