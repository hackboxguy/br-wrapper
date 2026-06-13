import QtQuick 2.12

PatternBase {
    patternName: "moving-ball"
    backgroundColor: "black"
    
    property var params: patternController ? patternController.patternParams : ({})
    property int ballSize: params.ballSize !== undefined ? params.ballSize : 50
    property int ballSpeed: params.ballSpeed !== undefined ? params.ballSpeed : 5
    property string ballDirection: params.ballDirection !== undefined ? params.ballDirection : "horizontal"
    property bool ballPaused: params.ballPaused !== undefined ? params.ballPaused : false
    
    // White ball
    Rectangle {
        id: ball
        width: ballSize
        height: ballSize
        radius: ballSize / 2
        color: "white"
        
        // Animation properties
        property real velocityX: ballDirection === "vertical" ? 0 : (ballSpeed * 0.5)
        property real velocityY: ballDirection === "horizontal" ? 0 : (ballSpeed * 0.5)
        
        // Initialize position and velocity
        Component.onCompleted: {
            if (ballDirection === "diagonal") {
                velocityX = ballSpeed * 0.35  // Slower for diagonal
                velocityY = ballSpeed * 0.35
            }
            // Start at center
            x = (parent.width - width) / 2
            y = (parent.height - height) / 2
        }
        
        // Animation timer for 60fps
        Timer {
            id: animationTimer
            interval: 16  // ~60fps (1000ms/60 = 16.67ms)
            running: !ballPaused
            repeat: true
            
            onTriggered: {
                // Update position
                ball.x += ball.velocityX
                ball.y += ball.velocityY
                
                // Bounce off edges
                if (ball.x <= 0 || ball.x >= parent.width - ball.width) {
                    ball.velocityX = -ball.velocityX
                    // Clamp position to bounds
                    ball.x = Math.max(0, Math.min(parent.width - ball.width, ball.x))
                }
                
                if (ball.y <= 0 || ball.y >= parent.height - ball.height) {
                    ball.velocityY = -ball.velocityY
                    // Clamp position to bounds
                    ball.y = Math.max(0, Math.min(parent.height - ball.height, ball.y))
                }
            }
        }
        
        // Update velocity when parameters change
        onBallSpeedChanged: updateVelocity()
        onBallDirectionChanged: updateVelocity()
        
        function updateVelocity() {
            if (ballDirection === "horizontal") {
                velocityX = velocityX > 0 ? ballSpeed * 0.5 : -ballSpeed * 0.5
                velocityY = 0
            } else if (ballDirection === "vertical") {
                velocityX = 0
                velocityY = velocityY > 0 ? ballSpeed * 0.5 : -ballSpeed * 0.5
            } else if (ballDirection === "diagonal") {
                var signX = velocityX > 0 ? 1 : -1
                var signY = velocityY > 0 ? 1 : -1
                velocityX = signX * ballSpeed * 0.35
                velocityY = signY * ballSpeed * 0.35
            }
        }
    }
}
