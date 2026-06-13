import QtQuick 2.12

PatternBase {
    patternName: "colorbar"

    // Main SMPTE color bars pattern
    Column {
        anchors.fill: parent

        // Top section - 75% amplitude color bars (2/3 of screen height)
        Row {
            width: parent.width
            height: parent.height * 2/3

            // White bar
            Rectangle {
                width: parent.width / 7
                height: parent.height
                color: "#c0c0c0"  // 75% white (192, 192, 192)
            }

            // Yellow bar
            Rectangle {
                width: parent.width / 7
                height: parent.height
                color: "#c0c000"  // 75% yellow (192, 192, 0)
            }

            // Cyan bar
            Rectangle {
                width: parent.width / 7
                height: parent.height
                color: "#00c0c0"  // 75% cyan (0, 192, 192)
            }

            // Green bar
            Rectangle {
                width: parent.width / 7
                height: parent.height
                color: "#00c000"  // 75% green (0, 192, 0)
            }

            // Magenta bar
            Rectangle {
                width: parent.width / 7
                height: parent.height
                color: "#c000c0"  // 75% magenta (192, 0, 192)
            }

            // Red bar
            Rectangle {
                width: parent.width / 7
                height: parent.height
                color: "#c00000"  // 75% red (192, 0, 0)
            }

            // Blue bar
            Rectangle {
                width: parent.width / 7
                height: parent.height
                color: "#0000c0"  // 75% blue (0, 0, 192)
            }
        }

        // Bottom section - Test patterns (1/3 of screen height)
        Row {
            width: parent.width
            height: parent.height * 1/3

            // Left section - Reverse blue bars (1/7 width)
            Column {
                width: parent.width / 7
                height: parent.height

                // Blue
                Rectangle {
                    width: parent.width
                    height: parent.height * 3/4
                    color: "#0000c0"  // 75% blue
                }
                // Black
                Rectangle {
                    width: parent.width
                    height: parent.height * 1/4
                    color: "#000000"  // Black
                }
            }

            // Middle-left - Reverse magenta bars (1/7 width)
            Column {
                width: parent.width / 7
                height: parent.height

                // Magenta
                Rectangle {
                    width: parent.width
                    height: parent.height * 3/4
                    color: "#c000c0"  // 75% magenta
                }
                // Black
                Rectangle {
                    width: parent.width
                    height: parent.height * 1/4
                    color: "#000000"  // Black
                }
            }

            // Center section - PLUGE and gray scale (3/7 width)
            Row {
                width: parent.width * 3/7
                height: parent.height

                // PLUGE section
                Column {
                    width: parent.width * 2/3
                    height: parent.height

                    // Gray background
                    Rectangle {
                        width: parent.width
                        height: parent.height * 3/4
                        color: "#404040"  // Mid gray
                    }

                    // PLUGE bars
                    Row {
                        width: parent.width
                        height: parent.height * 1/4

                        // Super black (-4 IRE)
                        Rectangle {
                            width: parent.width / 6
                            height: parent.height
                            color: "#000000"
                        }

                        // Black (0 IRE)
                        Rectangle {
                            width: parent.width / 6
                            height: parent.height
                            color: "#101010"
                        }

                        // Above black (+4 IRE)
                        Rectangle {
                            width: parent.width / 6
                            height: parent.height
                            color: "#1a1a1a"
                        }

                        // Black (0 IRE)
                        Rectangle {
                            width: parent.width / 6
                            height: parent.height
                            color: "#101010"
                        }

                        // Above black (+4 IRE)
                        Rectangle {
                            width: parent.width / 6
                            height: parent.height
                            color: "#1a1a1a"
                        }

                        // Black (0 IRE)
                        Rectangle {
                            width: parent.width / 6
                            height: parent.height
                            color: "#101010"
                        }
                    }
                }

                // White reference (100 IRE)
                Rectangle {
                    width: parent.width * 1/3
                    height: parent.height
                    color: "#ffffff"  // 100% white
                }
            }

            // Middle-right - Reverse cyan bars (1/7 width)
            Column {
                width: parent.width / 7
                height: parent.height

                // Cyan
                Rectangle {
                    width: parent.width
                    height: parent.height * 3/4
                    color: "#00c0c0"  // 75% cyan
                }
                // Black
                Rectangle {
                    width: parent.width
                    height: parent.height * 1/4
                    color: "#000000"  // Black
                }
            }

            // Right section - Reverse yellow bars (1/7 width)
            Column {
                width: parent.width / 7
                height: parent.height

                // Yellow
                Rectangle {
                    width: parent.width
                    height: parent.height * 3/4
                    color: "#c0c000"  // 75% yellow
                }
                // Black
                Rectangle {
                    width: parent.width
                    height: parent.height * 1/4
                    color: "#000000"  // Black
                }
            }
        }
    }
}
