import QtQuick 2.12

PatternBase {
    patternName: "starfield"
    backgroundColor: "black"
    
    property var params: patternController ? patternController.patternParams : ({})
    property int starfieldDensity: params.starfieldDensity !== undefined ? params.starfieldDensity : 100
    property int starfieldSeed: params.starfieldSeed !== undefined ? params.starfieldSeed : -1
    
    // Star container
    Item {
        id: starContainer
        anchors.fill: parent
        
        // Generate stars when component loads or parameters change
        Component.onCompleted: generateStars()
        onStarfieldDensityChanged: generateStars()
        onStarfieldSeedChanged: generateStars()
        
        function generateStars() {
            // Clear existing stars
            for (var i = starContainer.children.length - 1; i >= 0; i--) {
                if (starContainer.children[i].objectName === "star") {
                    starContainer.children[i].destroy()
                }
            }
            
            // Set random seed if specified
            var useRandomSeed = starfieldSeed >= 0
            if (useRandomSeed) {
                // Simple seedable random number generator
                var seed = starfieldSeed
                Math.random = function() {
                    seed = (seed * 9301 + 49297) % 233280
                    return seed / 233280
                }
            }
            
            // Create stars
            for (var i = 0; i < starfieldDensity; i++) {
                var star = starComponent.createObject(starContainer)
                if (star) {
                    star.objectName = "star"
                    star.x = Math.random() * (parent.width - 4)
                    star.y = Math.random() * (parent.height - 4)
                    // Vary star brightness slightly
                    star.opacity = 0.7 + Math.random() * 0.3
                }
            }
            
            // Restore normal random if we used seeded
            if (useRandomSeed) {
                // Reset to normal random (not perfect but good enough)
                Math.random = function() {
                    var x = Math.sin(Date.now()) * 10000
                    return x - Math.floor(x)
                }
            }
        }
    }
    
    // Star component
    Component {
        id: starComponent
        Rectangle {
            width: 4
            height: 4
            color: "white"
            radius: 2
        }
    }
}
