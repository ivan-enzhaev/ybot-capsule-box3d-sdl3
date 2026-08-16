#include "physics_debug.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

int b3InternalAssert(const char *condition, const char *fileName, int lineNumber)
{
    printf("Box3D Assertion Failed: %s, file %s, line %d\n", condition, fileName, lineNumber);
    fflush(stdout);
    abort();
    return 1;
}

void *createDebugShape(const b3DebugShape *debugShape, void *context)
{
    MyDebugShape *myShape = (MyDebugShape *)malloc(sizeof(MyDebugShape));
    myShape->type = debugShape->type;

    if (debugShape->type == b3_hullShape)
    {
        b3AABB aabb = debugShape->hull->aabb;

        myShape->extents.x = (aabb.upperBound.x - aabb.lowerBound.x) * 0.5f;
        myShape->extents.y = (aabb.upperBound.y - aabb.lowerBound.y) * 0.5f;
        myShape->extents.z = (aabb.upperBound.z - aabb.lowerBound.z) * 0.5f;
    }
    else if (debugShape->type == b3_sphereShape)
    {
        myShape->radius = debugShape->sphere->radius;
    }
    else if (debugShape->type == b3_capsuleShape)
    {
        myShape->radius = debugShape->capsule->radius;
        myShape->p1 = debugShape->capsule->center1;
        myShape->p2 = debugShape->capsule->center2;
    }

    return (void *)myShape;
}

void destroyDebugShape(void *userShape, void *context)
{
    free(userShape);
}

// Build local wireframe box lines (24 floats for 12 segments)
static void getWireframeBoxLines(vec3 extents, std::vector<float> &outLines)
{
    float x = extents[0], y = extents[1], z = extents[2];

    vec3 corners[8] = {
        { -x, -y, -z }, { x, -y, -z }, { x, y, -z }, { -x, y, -z },
        { -x, -y, z }, { x, -y, z }, { x, y, z }, { -x, y, z }
    };

    int indices[24] = {
        0, 1, 1, 2, 2, 3, 3, 0, // Bottom loop
        4, 5, 5, 6, 6, 7, 7, 4, // Top loop
        0, 4, 1, 5, 2, 6, 3, 7  // Vertical edges
    };

    outLines.reserve(outLines.size() + 24 * 3);
    for (int i = 0; i < 24; ++i)
    {
        outLines.push_back(corners[indices[i]][0]);
        outLines.push_back(corners[indices[i]][1]);
        outLines.push_back(corners[indices[i]][2]);
    }
}

bool drawShape(void *userShape, b3WorldTransform transform, b3HexColor color, void *context)
{
    if (!context || !userShape)
        return false;
    App *app = (App *)context;

    MyDebugShape *myShape = (MyDebugShape *)userShape;
    b3Vec3 pos = b3ToVec3(transform.p);

    // Build Model Matrix (Translation * Rotation) using cglm
    mat4 modelMatrix = GLM_MAT4_IDENTITY_INIT;
    glm_translate(modelMatrix, (float[]) { pos.x, pos.y, pos.z });

    versor q = { transform.q.v.x, transform.q.v.y, transform.q.v.z, transform.q.s };
    mat4 rot;
    glm_quat_mat4(q, rot);
    glm_mat4_mul(modelMatrix, rot, modelMatrix);

    // Final MVP = ProjView3D * Model
    mat4 mvp;
    glm_mat4_mul(app->projView3D, modelMatrix, mvp);

    std::vector<float> lineVertices;
    if (myShape->type == b3_hullShape)
    {
        vec3 extents = { myShape->extents.x, myShape->extents.y, myShape->extents.z };
        getWireframeBoxLines(extents, lineVertices);
    }
    else if (myShape->type == b3_sphereShape)
    {
        // Simple circle loop along XY for debug visual representation
        // const int segments = 16;
        // for (int i = 0; i < segments; ++i)
        // {
        //     float a1 = ((float)i / segments) * GLM_PIf * 2.0f;
        //     float a2 = ((float)(i + 1) / segments) * GLM_PIf * 2.0f;

        //     lineVertices.push_back(cosf(a1) * myShape->radius);
        //     lineVertices.push_back(sinf(a1) * myShape->radius);
        //     lineVertices.push_back(0.0f);

        //     lineVertices.push_back(cosf(a2) * myShape->radius);
        //     lineVertices.push_back(sinf(a2) * myShape->radius);
        //     lineVertices.push_back(0.0f);
        // }

        const int segments = 24;
        float r = myShape->radius;

        for (int i = 0; i < segments; ++i)
        {
            float a1 = ((float)i / segments) * GLM_PIf * 2.0f;
            float a2 = ((float)(i + 1) / segments) * GLM_PIf * 2.0f;

            float c1 = cosf(a1) * r, s1 = sinf(a1) * r;
            float c2 = cosf(a2) * r, s2 = sinf(a2) * r;

            // 1. XY Plane Circle
            lineVertices.push_back(c1);
            lineVertices.push_back(s1);
            lineVertices.push_back(0.0f);
            lineVertices.push_back(c2);
            lineVertices.push_back(s2);
            lineVertices.push_back(0.0f);

            // 2. XZ Plane Circle
            lineVertices.push_back(c1);
            lineVertices.push_back(0.0f);
            lineVertices.push_back(s1);
            lineVertices.push_back(c2);
            lineVertices.push_back(0.0f);
            lineVertices.push_back(s2);

            // 3. YZ Plane Circle
            lineVertices.push_back(0.0f);
            lineVertices.push_back(c1);
            lineVertices.push_back(s1);
            lineVertices.push_back(0.0f);
            lineVertices.push_back(c2);
            lineVertices.push_back(s2);
        }
    }
    else if (myShape->type == b3_capsuleShape)
    {
        const int segments = 16;
        float r = myShape->radius;
        b3Vec3 p1 = myShape->p1;
        b3Vec3 p2 = myShape->p2;

        // 1. Two main caps horizontal circles (at p1 and p2)
        for (int i = 0; i < segments; ++i)
        {
            float a1 = ((float)i / segments) * GLM_PIf * 2.0f;
            float a2 = ((float)(i + 1) / segments) * GLM_PIf * 2.0f;

            float c1 = cosf(a1) * r, s1 = sinf(a1) * r;
            float c2 = cosf(a2) * r, s2 = sinf(a2) * r;

            // Cap 1 (bottom) XZ circle
            lineVertices.push_back(p1.x + c1);
            lineVertices.push_back(p1.y);
            lineVertices.push_back(p1.z + s1);
            lineVertices.push_back(p1.x + c2);
            lineVertices.push_back(p1.y);
            lineVertices.push_back(p1.z + s2);

            // Cap 2 (top) XZ circle
            lineVertices.push_back(p2.x + c1);
            lineVertices.push_back(p2.y);
            lineVertices.push_back(p2.z + s1);
            lineVertices.push_back(p2.x + c2);
            lineVertices.push_back(p2.y);
            lineVertices.push_back(p2.z + s2);
        }

        // 2. Connecting vertical side lines along X and Z axes
        float offsets[4][2] = { { r, 0.0f }, { -r, 0.0f }, { 0.0f, r }, { 0.0f, -r } };
        for (int i = 0; i < 4; ++i)
        {
            lineVertices.push_back(p1.x + offsets[i][0]);
            lineVertices.push_back(p1.y);
            lineVertices.push_back(p1.z + offsets[i][1]);

            lineVertices.push_back(p2.x + offsets[i][0]);
            lineVertices.push_back(p2.y);
            lineVertices.push_back(p2.z + offsets[i][1]);
        }

        // 3. Top and Bottom vertical dome arches (XY & YZ half-circles)
        for (int i = 0; i < segments / 2; ++i)
        {
            float a1 = ((float)i / (segments / 2)) * GLM_PIf;
            float a2 = ((float)(i + 1) / (segments / 2)) * GLM_PIf;

            float c1 = cosf(a1) * r, s1 = sinf(a1) * r;
            float c2 = cosf(a2) * r, s2 = sinf(a2) * r;

            // Top dome arch
            lineVertices.push_back(p2.x + c1);
            lineVertices.push_back(p2.y + s1);
            lineVertices.push_back(p2.z);
            lineVertices.push_back(p2.x + c2);
            lineVertices.push_back(p2.y + s2);
            lineVertices.push_back(p2.z);

            // Bottom dome arch
            lineVertices.push_back(p1.x + c1);
            lineVertices.push_back(p1.y - s1);
            lineVertices.push_back(p1.z);
            lineVertices.push_back(p1.x + c2);
            lineVertices.push_back(p1.y - s2);
            lineVertices.push_back(p1.z);
        }
    }

    if (lineVertices.empty())
        return true;

    // Direct Pure OpenGL Render Call using app's line shader
    glUseProgram(app->lineShaderProgram);
    glUniformMatrix4fv(app->uLineMvpMatrixLocation, 1, GL_FALSE, (const GLfloat *)mvp);

    // Convert hex color to RGBA normalized floats
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = (color & 0xFF) / 255.0f;
    glUniform4f(app->uLineColorLocation, r, g, b, 1.0f);

    glBindVertexArray(app->lineVao);
    glBindBuffer(GL_ARRAY_BUFFER, app->lineVbo);
    glBufferData(GL_ARRAY_BUFFER, lineVertices.size() * sizeof(float), lineVertices.data(), GL_DYNAMIC_DRAW);

    glDrawArrays(GL_LINES, 0, (GLsizei)(lineVertices.size() / 3));

    glBindVertexArray(0);
    return true;
}
