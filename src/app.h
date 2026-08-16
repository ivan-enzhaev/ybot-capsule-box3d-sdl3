#pragma once

// OpenGL Loader
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
#include <GLES3/gl3.h>
#else
#include <glad/glad.h>
#endif // __EMSCRIPTEN__

// SDL3 Core & TTF
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

// Dear ImGui
#include <imgui.h>

// Math Library
#include <cglm/cglm.h>

// Ozz Animation Library
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/containers/vector.h>
#include <ozz/base/maths/simd_math.h>

// Physics Engine
#include <box3d/box3d.h>

// Standard C++ Headers
#include <vector>

struct Mat4Wrapper
{
    mat4 m;
};

struct App
{
    SDL_Window *window;
    SDL_GLContext glContext;
    GLuint shaderProgram;
    GLuint vao, vbo;
    GLuint textTextureID;
    TTF_Font *font;
    int textW;
    int textH;
    mat4 mvpMatrix;
    GLint uMvpMatrixLocation;
    ImGuiIO *io;
    float scale_factor;
    GLuint cubeVao, cubeVbo, cubeUvVbo, cubeEbo;
    GLsizei cubeIndexCount;
    GLenum cubeIndexType;
    mat4 projView2D;
    mat4 projView3D;
    float cubeRotationAngle;
    GLuint cubeTextureID;

    // Ozz-animation fields
    ozz::animation::Skeleton ozzSkeleton;
    ozz::animation::Animation ozzIdleAnimation;
    ozz::animation::Animation ozzWalkAnimation;
    ozz::animation::Animation ozzRunAnimation;
    ozz::animation::SamplingJob::Context ozzSamplingContext;
    float ozzWalkAnimationTime;
    float ozzRunAnimationTime;
    ozz::vector<ozz::math::Float4x4> ozzModelMatrices;
    GLuint lineShaderProgram;
    GLint uLineMvpMatrixLocation;
    GLint uLineColorLocation;
    GLuint lineVao, lineVbo;

    // Skinning Fields
    GLuint skinningShaderProgram;
    GLint uSkinningMvpMatrixLocation;
    GLint uJointMatricesLocation;
    GLuint ybotVao;
    GLuint ybotVbo;       // Positions
    GLuint ybotUvVbo;     // UVs
    GLuint ybotJointVbo;  // Joint Indices (ivec4)
    GLuint ybotWeightVbo; // Joint Weights (vec4)
    GLuint ybotEbo;       // Indices
    GLsizei ybotIndexCount;
    GLenum ybotIndexType;
    GLuint ybotTextureID;
    std::vector<Mat4Wrapper> ybotInverseBindMatrices;
    bool showDebugSkeleton;
    bool showRobot;
    bool showPhysicsDebug;

    // Physics Engine Fields
    b3WorldId worldId;
    b3DebugDraw dd;
    b3BodyId capsuleId;

    // Input state flags
    bool moveUp = false;    // W / Up
    bool moveDown = false;  // S / Down
    bool moveLeft = false;  // A / Left
    bool moveRight = false; // D / Right

    float joystickX = 0.0f; // Range: -1.0 to 1.0
    float joystickY = 0.0f; // Range: -1.0 to 1.0

    float moveX_joystick = 0.0f;
    float moveZ_joystick = 0.0f;

    bool isMobile;
    bool needsReposition;
};
