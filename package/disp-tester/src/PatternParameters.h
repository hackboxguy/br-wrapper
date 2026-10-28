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
    bool whiteboxmmVerbose = false;     // Show the top-left size readout (set via "verbose" arg)

    // Measurement box ("pattern box <area-pct>|<N>px <r> <g> <b> [bg <r> <g> <b>] [at <x> <y>]"): a
    // square of one colour covering area-pct of the screen area (or N px a side), on a background
    // colour (sets the APL), centred on the screen or on pixel x,y (the sensor locator's sweep)
    double boxAreaPct = 10.0;
    int boxSidePx = 0;              // > 0: the side in pixels instead of boxAreaPct
    int boxCx = -1, boxCy = -1;     // centre in pixels (-1 = screen centre)
    int boxR = 255, boxG = 255, boxB = 255;
    int boxBgR = 0, boxBgG = 0, boxBgB = 0;

    // Checkerboard ("pattern checker <cols> <rows> [<r> <g> <b> <r> <g> <b>]"): cols x rows tiles over
    // the whole screen, the top-left tile in the first colour (default black, the other white); an odd
    // board has its centre tile in the first colour (3x3 on 1920x1080: 640x360 tiles, the in-scene black)
    int checkerCols = 3, checkerRows = 3;
    int checkerAR = 0, checkerAG = 0, checkerAB = 0;
    int checkerBR = 255, checkerBG = 255, checkerBB = 255;

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
        map["whiteboxmmVerbose"] = whiteboxmmVerbose;
        map["boxAreaPct"] = boxAreaPct;
        map["boxSidePx"] = boxSidePx;
        map["boxCx"] = boxCx;
        map["boxCy"] = boxCy;
        map["boxR"] = boxR;
        map["boxG"] = boxG;
        map["boxB"] = boxB;
        map["boxBgR"] = boxBgR;
        map["boxBgG"] = boxBgG;
        map["boxBgB"] = boxBgB;
        map["checkerCols"] = checkerCols;
        map["checkerRows"] = checkerRows;
        map["checkerAR"] = checkerAR;
        map["checkerAG"] = checkerAG;
        map["checkerAB"] = checkerAB;
        map["checkerBR"] = checkerBR;
        map["checkerBG"] = checkerBG;
        map["checkerBB"] = checkerBB;
        return map;
    }
};

#endif // PATTERNPARAMETERS_H
