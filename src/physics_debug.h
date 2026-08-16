#pragma once

#include "app.h"
#include <box3d/box3d.h>

// Debug shape storage structure
typedef struct
{
    b3ShapeType type;
    b3Vec3 extents; // For boxes (half-extents)
    float radius;   // For spheres
    b3Vec3 p1, p2;  // For capsule cap centers
} MyDebugShape;

// Box3D Assertion Handler
int b3InternalAssert(const char *condition, const char *fileName, int lineNumber);

// Box3D Debug Draw Callbacks
void *createDebugShape(const b3DebugShape *debugShape, void *context);
void destroyDebugShape(void *userShape, void *context);
bool drawShape(void *userShape, b3WorldTransform transform, b3HexColor color, void *context);
