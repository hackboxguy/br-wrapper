import QtQuick 2.12

NeedleGauge {
    value: cluster.rpm
    minValue: 0
    maxValue: 8000
    startAngle: -135
    endAngle: 135
    majorTickInterval: 1000
    minorTickInterval: 500
    labelDivisor: 1000
    label: "RPM x1000"
    showDigitalValue: false
    redlineValue: 6500
}
