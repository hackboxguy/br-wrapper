#ifndef PATTERNPARAMETERS_H
#define PATTERNPARAMETERS_H

#include <QObject>
#include <QVariantMap>

struct PatternParameters {
    // Moving Ball parameters
    int ballSize = 50;              // Ball diameter in pixels
    int ballSpeed = 5;              // Speed level 1-10
    QString ballDirection = "horizontal"; // horizontal/vertical/diagonal
    bool ballPaused = false;
    
    // Starfield parameters
    int starfieldDensity = 100;     // Number of stars
    int starfieldSeed = -1;         // Random seed (-1 = random)
    
    // Zone Grid parameters
    int gridSpacing = 0;            // 0 = auto-calculate
    int gridZonesX = 16;            // Default zone count X
    int gridZonesY = 9;             // Default zone count Y
    int gridHighlight = -1;         // Highlighted zone (-1 = none)
    
    // Blooming Detection parameters
    int bloomingX = -1;             // Pixel X position (-1 = center)
    int bloomingY = -1;             // Pixel Y position (-1 = center)
    
    // Cross Dimming parameters
    int crossDimmingSpots = 4;      // Number of bright spots
    
    QVariantMap toVariantMap() const {
        QVariantMap map;
        map["ballSize"] = ballSize;
        map["ballSpeed"] = ballSpeed;
        map["ballDirection"] = ballDirection;
        map["ballPaused"] = ballPaused;
        map["starfieldDensity"] = starfieldDensity;
        map["starfieldSeed"] = starfieldSeed;
        map["gridSpacing"] = gridSpacing;
        map["gridZonesX"] = gridZonesX;
        map["gridZonesY"] = gridZonesY;
        map["gridHighlight"] = gridHighlight;
        map["bloomingX"] = bloomingX;
        map["bloomingY"] = bloomingY;
        map["crossDimmingSpots"] = crossDimmingSpots;
        return map;
    }
};

#endif // PATTERNPARAMETERS_H
