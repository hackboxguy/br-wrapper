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

    // White Box parameters
    int whiteboxSize = 10;          // Box size as percentage of screen (1-50)
    QString whiteboxMode = "percent"; // Sizing mode: "percent", "pixels", or "mm"
    int whiteboxPixels = 100;       // Absolute pixel size (for pixels mode)
    float whiteboxMM = 50.0f;       // Physical size in mm (for mm mode)
    float whiteboxDiagonalInch = 0.0f; // Display diagonal in inches (for mm mode)

    // White Box MM parameters (new "whiteboxmm" pattern: absolute physical size from
    // explicit active-area dimensions; independent of the legacy "whitebox" pattern)
    float whiteboxmmSize = 50.0f;       // Requested box side in mm (1-500)
    float whiteboxmmPhysWidthMM = 0.0f;  // Active-area physical width in mm (from datasheet)
    float whiteboxmmPhysHeightMM = 0.0f; // Active-area physical height in mm (from datasheet)

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
        map["whiteboxSize"] = whiteboxSize;
        map["whiteboxMode"] = whiteboxMode;
        map["whiteboxPixels"] = whiteboxPixels;
        map["whiteboxMM"] = whiteboxMM;
        map["whiteboxDiagonalInch"] = whiteboxDiagonalInch;
        map["whiteboxmmSize"] = whiteboxmmSize;
        map["whiteboxmmPhysWidthMM"] = whiteboxmmPhysWidthMM;
        map["whiteboxmmPhysHeightMM"] = whiteboxmmPhysHeightMM;
        return map;
    }
};

#endif // PATTERNPARAMETERS_H
