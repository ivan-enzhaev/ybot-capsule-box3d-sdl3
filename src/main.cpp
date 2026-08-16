#define SDL_MAIN_USE_CALLBACKS 1 // Use the callbacks instead of main()

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <cglm/cglm.h>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
#include <GLES3/gl3.h>
#else
#include <glad/glad.h>
#endif // __EMSCRIPTEN__

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-attributes"
#include <ozz/animation/runtime/blending_job.h>
#pragma GCC diagnostic pop

#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/containers/vector.h>
#include <ozz/base/io/archive.h>
#include <ozz/base/maths/soa_transform.h>

#include "app.h"
#include "file_utils.h"
#include "ozz_sdl_stream.h"
#include "physics_debug.h"
#include "projection_utils.h"
#include "shader_program.h"
#include "texture_utils.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// Defines a C++ function that runs JS in WebAssembly builds
EM_JS(bool, EMSCRIPTEN_IsMobileBrowser, (), {
    return /iPhone|iPad|iPod|Android|webOS|BlackBerry|IEMobile|Opera Mini/i.test(navigator.userAgent);
});
#endif

// Universal helper function across platforms
bool isMobilePlatform()
{
#if defined(__ANDROID__) || defined(Q_OS_IOS)
    return true;
#elif defined(__EMSCRIPTEN__)
    return EMSCRIPTEN_IsMobileBrowser();
#else
    return false;
#endif
}

// This function runs once at startup
SDL_AppResult SDL_AppInit(void **appState, int argc, char *argv[])
{
    // App *app = (App *)SDL_malloc(sizeof(App));
    App *app = new App();
    *appState = app;

#ifndef __EMSCRIPTEN__
    if (!SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, "60"))
    {
        SDL_Log("Failed to set a frame rate: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
#endif

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!TTF_Init())
    {
        SDL_Log("Couldn't initialize SDL_ttf: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8); // Recommended if using ImGui/stencils

    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1); // Enable MULTISAMPLE
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 8); // Can be 2, 4, 8 or 16

#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
    // Mobile and Web: Request OpenGL ES 3.0
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
    // Windows/Desktop: Request OpenGL 3.3 Core Profile
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    // Explicitly ask for forward compatibility for better driver support
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
    int w = 600; // Default width for Windows
    int h = 480; // Default height for Windows

    // #if __ANDROID__
    //     // flags |= (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_RESIZABLE);
    //     flags |= SDL_WINDOW_FULLSCREEN;
    //     w = 0;
    //     h = 0;
    // #endif // __ANDROID__

    // Detect mobile native or mobile web browser
    if (isMobilePlatform())
    {
        flags |= SDL_WINDOW_FULLSCREEN;
        w = 0;
        h = 0;
    }

    app->window = SDL_CreateWindow("SDL3, OpenGL, ozz-animation", w, h, flags);
    if (!app->window)
    {
        SDL_Log("Couldn't create the window: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    app->glContext = SDL_GL_CreateContext(app->window);
    if (!app->glContext)
    {
        SDL_Log("Couldn't create the glContext: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

#ifdef WIN32
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
    {
        SDL_Log("Failed to initialize OpenGL function pointers");
        return SDL_APP_FAILURE;
    }
#endif // WIN32

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // int depthBits = 0;
    // SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &depthBits);
    // SDL_Log("Allocated Depth Buffer Size: %d bits", depthBits);

    // Load shader sources
#if defined(__ANDROID__)
    // On Android, the assets folder is the root
    char *vertexSource = readFile("shaders/texture.vert");
    char *fragmentSource = readFile("shaders/texture.frag");
#else
    // On Windows/Desktop, you might still use the "assets/" prefix
    // if a local folder structure keeps them there
    char *vertexSource = readFile("assets/shaders/texture.vert");
    char *fragmentSource = readFile("assets/shaders/texture.frag");
#endif

    // Validate that both loaded successfully
    if (vertexSource != NULL && fragmentSource != NULL)
    {
        // Pass the pointers to your creation function
        app->shaderProgram = createShaderProgram(vertexSource, fragmentSource);
        if (!app->shaderProgram)
        {
            return SDL_APP_FAILURE;
        }

        // Once the GPU has compiled the shaders, you can free the memory
        SDL_free(vertexSource);
        SDL_free(fragmentSource);
    }
    else
    {
        // Handle error: one or both shaders failed to load
        if (vertexSource)
            SDL_free(vertexSource);
        if (fragmentSource)
            SDL_free(fragmentSource);

        SDL_Log("Error: Could not load shader files.");
        return SDL_APP_FAILURE;
    }

    glUseProgram(app->shaderProgram);

    app->uMvpMatrixLocation = glGetUniformLocation(app->shaderProgram, "uMvpMatrix");

    GLint textureLocation = glGetUniformLocation(app->shaderProgram, "ourTexture");
    glUniform1i(textureLocation, 0); // Explicitly set "ourTexture" to use GL_TEXTURE0

    // Load Line Shaders
#if defined(__ANDROID__)
    char *lineVertSource = readFile("shaders/line.vert");
    char *lineFragSource = readFile("shaders/line.frag");
#else
    char *lineVertSource = readFile("assets/shaders/line.vert");
    char *lineFragSource = readFile("assets/shaders/line.frag");
#endif

    if (lineVertSource != NULL && lineFragSource != NULL)
    {
        app->lineShaderProgram = createShaderProgram(lineVertSource, lineFragSource);
        if (!app->lineShaderProgram)
        {
            return SDL_APP_FAILURE;
        }

        SDL_free(lineVertSource);
        SDL_free(lineFragSource);
    }
    else
    {
        if (lineVertSource)
            SDL_free(lineVertSource);
        if (lineFragSource)
            SDL_free(lineFragSource);

        SDL_Log("Error: Could not load line shader files.");
        return SDL_APP_FAILURE;
    }

    // Load Skinning Shaders
#if defined(__ANDROID__)
    char *skinVertSource = readFile("shaders/skinning.vert");
    char *skinFragSource = readFile("shaders/skinning.frag");
#else
    char *skinVertSource = readFile("assets/shaders/skinning.vert");
    char *skinFragSource = readFile("assets/shaders/skinning.frag");
#endif

    if (skinVertSource != NULL && skinFragSource != NULL)
    {
        app->skinningShaderProgram = createShaderProgram(skinVertSource, skinFragSource);
        if (!app->skinningShaderProgram)
        {
            return SDL_APP_FAILURE;
        }

        SDL_free(skinVertSource);
        SDL_free(skinFragSource);
    }
    else
    {
        if (skinVertSource)
            SDL_free(skinVertSource);
        if (skinFragSource)
            SDL_free(skinFragSource);

        SDL_Log("Error: Could not load skinning shader files.");
        return SDL_APP_FAILURE;
    }

    glUseProgram(app->skinningShaderProgram);

    // Cache the skinning shader uniform locations
    app->uSkinningMvpMatrixLocation = glGetUniformLocation(app->skinningShaderProgram, "uMvpMatrix");
    app->uJointMatricesLocation = glGetUniformLocation(app->skinningShaderProgram, "uJointMatrices");

    GLint skinningTextureLocation = glGetUniformLocation(app->skinningShaderProgram, "ourTexture");
    glUniform1i(skinningTextureLocation, 0); // Explicitly set "ourTexture" to use GL_TEXTURE0

    glUseProgram(app->lineShaderProgram);

    // Cache the line shader uniform locations
    app->uLineMvpMatrixLocation = glGetUniformLocation(app->lineShaderProgram, "uMvpMatrix");
    app->uLineColorLocation = glGetUniformLocation(app->lineShaderProgram, "uColor");

    // Setup VAO/VBO for dynamic skeleton lines
    glGenVertexArrays(1, &app->lineVao);
    glGenBuffers(1, &app->lineVbo);

    glBindVertexArray(app->lineVao);
    glBindBuffer(GL_ARRAY_BUFFER, app->lineVbo);
    // Allocate an initial placeholder buffer (e.g., space for 100 joints * 2 points * 3 floats)
    glBufferData(GL_ARRAY_BUFFER, 200 * 3 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    // Position attribute (layout location 0 in line.vert)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);

    glBindVertexArray(0);

    // VAO/VBO Setup (Unit Quad 0.0 to 1.0)
    float vertices[] = {
        // x, y, u, v (flipped)
        -0.5f, 0.5f, 0.0f, 0.0f,
        0.5f, -0.5f, 1.0f, 1.0f,
        -0.5f, -0.5f, 0.0f, 1.0f,

        -0.5f, 0.5f, 0.0f, 0.0f,
        0.5f, 0.5f, 1.0f, 0.0f,
        0.5f, -0.5f, 1.0f, 1.0f
    };
    glGenVertexArrays(1, &app->vao);
    glGenBuffers(1, &app->vbo);
    glBindVertexArray(app->vao);
    glBindBuffer(GL_ARRAY_BUFFER, app->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // Stride is 4 floats (x, y, u, v) = 16 bytes
    GLsizei stride = 4 * sizeof(float);

    // Position (Location 0, 2 components: x, y)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void *)0);
    glEnableVertexAttribArray(0);

    // Texture Coordinates (Location 1, 2 components: u, v)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    // Load Font
#if defined(__ANDROID__)
    app->font = TTF_OpenFont("fonts/LiberationSans-Regular.ttf", 36.0f);
#else
    app->font = TTF_OpenFont("assets/fonts/LiberationSans-Regular.ttf", 36.0f);
#endif

    if (!app->font)
    {
        SDL_Log("Font error: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    // Render text to surface, then to GL texture
    SDL_Color color = { 170, 255, 195, 255 };
    SDL_Surface *s = TTF_RenderText_Blended_Wrapped(app->font,
        "Hello, SDL3_ttf!\nПривет, SDL3_ttf!", 0, color, 0);
    if (s)
    {
        // Convert surface to a guaranteed RGBA format
        SDL_Surface *converted = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(s); // Free the original

        if (converted)
        {
            app->textW = converted->w;
            app->textH = converted->h;
            app->textTextureID = createTextureFromSurface(converted);
            SDL_DestroySurface(converted);
        }
    }

    updateProjectionAndMVP(app);
    update3DProjectionAndMVP(app);

    // Setup Dear ImGui Context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    app->io = &ImGui::GetIO();
    (void)app->io;
    ImGui::StyleColorsDark();

    // Query SDL3 for the actual platform display scale
    // (Handles Windows desktop DPI, Android screen densities, and Wasm/Browser pixel ratios)
    app->scale_factor = SDL_GetWindowDisplayScale(app->window);
    if (app->scale_factor <= 0.0f)
    {
        app->scale_factor = 1.0f;
    }

    if (app->isMobile)
    {
        // Apply mobile-specific DPI scaling adjustments dynamically across APK and Wasm
        app->scale_factor *= 1.0f;
    }

    // Load custom TTF font for ImGui safely across all platforms (including Android assets)
    const char *font_path =
#if defined(__ANDROID__)
        "fonts/LiberationSans-Regular.ttf";
#else
        "assets/fonts/LiberationSans-Regular.ttf";
#endif

    size_t font_size = 0;
    void *font_data = SDL_LoadFile(font_path, &font_size);
    if (font_data)
    {
        // AddFontFromMemoryTTF takes ownership of the buffer and frees it automatically when the atlas is cleared
        app->io->Fonts->AddFontFromMemoryTTF(font_data, (int)font_size, 24.0f * app->scale_factor);
    }
    else
    {
        SDL_Log("Failed to load ImGui font from %s: %s", font_path, SDL_GetError());
        app->io->Fonts->AddFontDefault();
    }

    ImGui::GetStyle().ScaleAllSizes(app->scale_factor);

    // Determine the GLSL version string based on the target platform
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
    const char *glsl_version = "#version 300 es";
#else
    const char *glsl_version = "#version 330 core";
#endif

    // Initialize ImGui SDL3 backend
    ImGui_ImplSDL3_InitForOpenGL(app->window, app->glContext);

    // Initialize ImGui OpenGL3 backend with the dynamic version string
    ImGui_ImplOpenGL3_Init(glsl_version);

    cgltf_options options = {};
    cgltf_data *data = NULL;

    const char *cubePath =
#if defined(__ANDROID__)
        "models/cube/check-grid-cube.glb";
#else
        "assets/models/cube/check-grid-cube.glb";
#endif

    size_t glb_size = 0;
    void *glb_data = SDL_LoadFile(cubePath, &glb_size);

    if (!glb_data)
    {
        SDL_Log("Failed to load model file into memory: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    cgltf_result result = cgltf_parse(&options, glb_data, glb_size, &data);

    if (result == cgltf_result_success)
    {
        cgltf_load_buffers(&options, data, cubePath);

        cgltf_mesh &mesh = data->meshes[0];
        cgltf_primitive &prim = mesh.primitives[0];

        cgltf_accessor *posAccessor = nullptr;
        cgltf_accessor *uvAccessor = nullptr;
        cgltf_accessor *indexAccessor = nullptr;

        for (int i = 0; i < prim.attributes_count; ++i)
        {
            if (prim.attributes[i].type == cgltf_attribute_type_position)
            {
                posAccessor = prim.attributes[i].data;
            }

            if (prim.attributes[i].type == cgltf_attribute_type_texcoord)
            {
                uvAccessor = prim.attributes[i].data;
            }
        }
        indexAccessor = prim.indices;

        float *positions = (float *)((uint8_t *)posAccessor->buffer_view->buffer->data +
                                     posAccessor->buffer_view->offset +
                                     posAccessor->offset);

        // OpenGL Buffer Setup
        glGenVertexArrays(1, &app->cubeVao);
        glBindVertexArray(app->cubeVao);

        size_t posSize = posAccessor->count * 3 * sizeof(float);
        float *positionsCopy = (float *)SDL_malloc(posSize);
        memcpy(positionsCopy, positions, posSize);

        // Vertex Buffer Object (Positions) - Layout location 0
        glGenBuffers(1, &app->cubeVbo);
        glBindBuffer(GL_ARRAY_BUFFER, app->cubeVbo);
        // glBufferData(GL_ARRAY_BUFFER, posAccessor->count * 3 * sizeof(float), positions, GL_STATIC_DRAW);
        glBufferData(GL_ARRAY_BUFFER, posSize, positionsCopy, GL_STATIC_DRAW);
        SDL_free(positionsCopy);

        // Setup position attribute
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);

        // Extract UV coordinates
        float *uvs = nullptr;
        if (uvAccessor)
        {
            uvs = (float *)((uint8_t *)uvAccessor->buffer_view->buffer->data +
                            uvAccessor->buffer_view->offset +
                            uvAccessor->offset);

            size_t uvSize = uvAccessor->count * 2 * sizeof(float);
            float *uvsCopy = (float *)SDL_malloc(uvSize);
            memcpy(uvsCopy, uvs, uvSize);

            glGenBuffers(1, &app->cubeUvVbo);
            glBindBuffer(GL_ARRAY_BUFFER, app->cubeUvVbo);
            glBufferData(GL_ARRAY_BUFFER, uvSize, uvsCopy, GL_STATIC_DRAW);
            SDL_free(uvsCopy);

            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
        }

        // Handle indices (could be unsigned int, unsigned short, or unsigned byte)
        void *indices = nullptr;
        GLenum indexType = GL_UNSIGNED_SHORT;
        if (indexAccessor)
        {
            indices = (void *)((uint8_t *)indexAccessor->buffer_view->buffer->data +
                               indexAccessor->buffer_view->offset +
                               indexAccessor->offset);

            size_t indexSize = indexAccessor->count * indexAccessor->stride;
            void *indicesCopy = SDL_malloc(indexSize);
            memcpy(indicesCopy, indices, indexSize);

            glGenBuffers(1, &app->cubeEbo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, app->cubeEbo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexSize, indicesCopy, GL_STATIC_DRAW);
            SDL_free(indicesCopy);

            if (indexAccessor->component_type == cgltf_component_type_r_32u)
            {
                indexType = GL_UNSIGNED_INT;
            }
            else if (indexAccessor->component_type == cgltf_component_type_r_16u)
            {
                indexType = GL_UNSIGNED_SHORT;
            }
            else if (indexAccessor->component_type == cgltf_component_type_r_8u)
            {
                indexType = GL_UNSIGNED_BYTE;
            }
        }

        glBindVertexArray(0);

        // Save index count for drawing later
        app->cubeIndexCount = (GLsizei)indexAccessor->count;
        app->cubeIndexType = indexType;

        const char *cubeTexturePath =
#if defined(__ANDROID__)
            "models/cube/check-grid-512.webp";
#else
            "assets/models/cube/check-grid-512.webp";
#endif
        app->cubeTextureID = createTexture(cubeTexturePath);

        cgltf_free(data); // Free cgltf parse structures
        SDL_free(glb_data);
    }
    else
    {
        SDL_Log("Failed to load a model: %s", cubePath);
        return SDL_APP_FAILURE;
    }

    cgltf_options ybotOptions = {};
    cgltf_data *ybotData = NULL;

    const char *ybotPath =
#if defined(__ANDROID__)
        "models/y-bot/y-bot.glb";
#else
        "assets/models/y-bot/y-bot.glb";
    // "assets/models/animated-plane/animated_plane.glb";
#endif

    size_t ybotGlbSize = 0;
    void *ybotGlbData = SDL_LoadFile(ybotPath, &ybotGlbSize);

    if (!ybotGlbData)
    {
        SDL_Log("Failed to load Y-Bot model file into memory: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    cgltf_result ybotResult = cgltf_parse(&ybotOptions, ybotGlbData, ybotGlbSize, &ybotData);

    if (ybotResult == cgltf_result_success)
    {
        cgltf_load_buffers(&ybotOptions, ybotData, ybotPath);

        // Assuming the mesh and primitive you want are at index 0 (adjust if Y-Bot has multiple primitives)
        cgltf_mesh &mesh = ybotData->meshes[0];
        cgltf_primitive &prim = mesh.primitives[0];

        cgltf_accessor *posAccessor = nullptr;
        cgltf_accessor *uvAccessor = nullptr;
        cgltf_accessor *jointsAccessor = nullptr;
        cgltf_accessor *weightsAccessor = nullptr;
        cgltf_accessor *indexAccessor = nullptr;

        for (int i = 0; i < prim.attributes_count; ++i)
        {
            if (prim.attributes[i].type == cgltf_attribute_type_position)
            {
                posAccessor = prim.attributes[i].data;
            }
            else if (prim.attributes[i].type == cgltf_attribute_type_texcoord)
            {
                uvAccessor = prim.attributes[i].data;
            }
            else if (prim.attributes[i].type == cgltf_attribute_type_joints)
            {
                jointsAccessor = prim.attributes[i].data;
            }
            else if (prim.attributes[i].type == cgltf_attribute_type_weights)
            {
                weightsAccessor = prim.attributes[i].data;
            }
        }
        indexAccessor = prim.indices;

        // Setup VAO
        glGenVertexArrays(1, &app->ybotVao);
        glBindVertexArray(app->ybotVao);

        // Positions (Location 0)
        if (posAccessor)
        {
            float *positions = (float *)((uint8_t *)posAccessor->buffer_view->buffer->data +
                                         posAccessor->buffer_view->offset +
                                         posAccessor->offset);
            size_t posSize = posAccessor->count * 3 * sizeof(float);
            float *positionsCopy = (float *)SDL_malloc(posSize);
            memcpy(positionsCopy, positions, posSize);

            // Print position data
            // SDL_Log("--- Position Buffer (%zu vertices) ---", posAccessor->count);
            // for (size_t i = 0; i < posAccessor->count; ++i)
            // {
            //     float x = positionsCopy[i * 3 + 0];
            //     float y = positionsCopy[i * 3 + 1];
            //     float z = positionsCopy[i * 3 + 2];
            //     SDL_Log("Vertex %zu: (%.3f, %.3f, %.3f)", i, x, y, z);
            // }

            glGenBuffers(1, &app->ybotVbo);
            glBindBuffer(GL_ARRAY_BUFFER, app->ybotVbo);
            glBufferData(GL_ARRAY_BUFFER, posSize, positionsCopy, GL_STATIC_DRAW);
            SDL_free(positionsCopy);

            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
        }

        // Texture Coordinates (Location 1)
        if (uvAccessor)
        {
            float *uvs = (float *)((uint8_t *)uvAccessor->buffer_view->buffer->data +
                                   uvAccessor->buffer_view->offset +
                                   uvAccessor->offset);
            size_t uvSize = uvAccessor->count * 2 * sizeof(float);
            float *uvsCopy = (float *)SDL_malloc(uvSize);
            memcpy(uvsCopy, uvs, uvSize);

            // Print UV coordinates for debugging
            // SDL_Log("--- UV Coordinates (Total Count: %d) ---", uvAccessor->count);
            // for (size_t i = 0; i < uvAccessor->count; ++i)
            // {
            //     float u = uvsCopy[i * 2 + 0];
            //     float v = uvsCopy[i * 2 + 1];
            //     SDL_Log("UV %zu -> U: %.3f, V: %.3f", i, u, v);
            // }

            glGenBuffers(1, &app->ybotUvVbo);
            glBindBuffer(GL_ARRAY_BUFFER, app->ybotUvVbo);
            glBufferData(GL_ARRAY_BUFFER, uvSize, uvsCopy, GL_STATIC_DRAW);
            SDL_free(uvsCopy);

            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
        }

        // Joint Indices (Location 2 - ivec4)
        if (jointsAccessor)
        {
            // Mixamo/glTF usually exports joints as unsigned shorts (USHORT) or unsigned bytes (UBYTE) vectors of 4 components
            uint8_t *jointsData = (uint8_t *)jointsAccessor->buffer_view->buffer->data +
                                  jointsAccessor->buffer_view->offset +
                                  jointsAccessor->offset;
            size_t jointsSize = jointsAccessor->count * jointsAccessor->stride;
            void *jointsCopy = SDL_malloc(jointsSize);
            memcpy(jointsCopy, jointsData, jointsSize);

            // Print joint data
            // SDL_Log("--- Joint Buffer (%zu vertices) ---", jointsAccessor->count);
            // for (size_t i = 0; i < jointsAccessor->count; ++i)
            // {
            //     if (jointsAccessor->component_type == cgltf_component_type_r_8u)
            //     {
            //         uint8_t *j = (uint8_t *)jointsCopy;
            //         SDL_Log("Vertex %zu: (%u, %u, %u, %u)", i,
            //             j[i * 4 + 0], j[i * 4 + 1], j[i * 4 + 2], j[i * 4 + 3]);
            //     }
            //     else
            //     {
            //         uint16_t *j = (uint16_t *)jointsCopy;
            //         SDL_Log("Vertex %zu: (%u, %u, %u, %u)", i,
            //             j[i * 4 + 0], j[i * 4 + 1], j[i * 4 + 2], j[i * 4 + 3]);
            //     }
            // }

            glGenBuffers(1, &app->ybotJointVbo);
            glBindBuffer(GL_ARRAY_BUFFER, app->ybotJointVbo);
            glBufferData(GL_ARRAY_BUFFER, jointsSize, jointsCopy, GL_STATIC_DRAW);
            SDL_free(jointsCopy);

            GLenum jointGlType = GL_UNSIGNED_SHORT;
            size_t componentSize = sizeof(unsigned short);

            if (jointsAccessor->component_type == cgltf_component_type_r_8u)
            {
                jointGlType = GL_UNSIGNED_BYTE;
                componentSize = sizeof(unsigned char);
            }
            else if (jointsAccessor->component_type == cgltf_component_type_r_16u)
            {
                jointGlType = GL_UNSIGNED_SHORT;
                componentSize = sizeof(unsigned short);
            }
            // SDL_Log("Joint GL type: 0x%X (Decimal: %u)", jointGlType, jointGlType);

            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 4, jointGlType, GL_FALSE, 4 * componentSize, (void *)0);
        }

        // Joint Weights (Location 3 - vec4)
        if (weightsAccessor)
        {
            float *weightsData = (float *)((uint8_t *)weightsAccessor->buffer_view->buffer->data +
                                           weightsAccessor->buffer_view->offset +
                                           weightsAccessor->offset);
            size_t weightsSize = weightsAccessor->count * 4 * sizeof(float);
            float *weightsCopy = (float *)SDL_malloc(weightsSize);
            memcpy(weightsCopy, weightsData, weightsSize);

            // Print weight data
            // SDL_Log("--- Weight Buffer (%zu vertices) ---", weightsAccessor->count);
            // for (size_t i = 0; i < weightsAccessor->count; ++i)
            // {
            //     float w0 = weightsCopy[i * 4 + 0];
            //     float w1 = weightsCopy[i * 4 + 1];
            //     float w2 = weightsCopy[i * 4 + 2];
            //     float w3 = weightsCopy[i * 4 + 3];
            //     SDL_Log("Vertex %zu: (%.2f, %.2f, %.2f, %.2f)", i, w0, w1, w2, w3);
            // }

            glGenBuffers(1, &app->ybotWeightVbo);
            glBindBuffer(GL_ARRAY_BUFFER, app->ybotWeightVbo);
            glBufferData(GL_ARRAY_BUFFER, weightsSize, weightsCopy, GL_STATIC_DRAW);
            SDL_free(weightsCopy);

            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
        }

        // Index Buffer (EBO)
        GLenum indexType = GL_UNSIGNED_SHORT;
        if (indexAccessor)
        {
            void *indices = (void *)((uint8_t *)indexAccessor->buffer_view->buffer->data +
                                     indexAccessor->buffer_view->offset +
                                     indexAccessor->offset);
            size_t indexSize = indexAccessor->count * indexAccessor->stride;
            void *indicesCopy = SDL_malloc(indexSize);
            memcpy(indicesCopy, indices, indexSize);

            // Print index data
            // SDL_Log("--- Index Buffer (%zu indices) ---", indexAccessor->count);
            // for (size_t i = 0; i < indexAccessor->count; ++i)
            // {
            //     uint32_t idxVal = 0;
            //     if (indexType == GL_UNSIGNED_INT)
            //     {
            //         idxVal = ((uint32_t *)indicesCopy)[i];
            //     }
            //     else if (indexType == GL_UNSIGNED_SHORT)
            //     {
            //         idxVal = ((uint16_t *)indicesCopy)[i];
            //     }
            //     else if (indexType == GL_UNSIGNED_BYTE)
            //     {
            //         idxVal = ((uint8_t *)indicesCopy)[i];
            //     }
            //     SDL_Log("Index %zu: %u", i, idxVal);
            // }

            glGenBuffers(1, &app->ybotEbo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, app->ybotEbo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexSize, indicesCopy, GL_STATIC_DRAW);
            SDL_free(indicesCopy);

            if (indexAccessor->component_type == cgltf_component_type_r_32u)
            {
                indexType = GL_UNSIGNED_INT;
            }
            else if (indexAccessor->component_type == cgltf_component_type_r_16u)
            {
                indexType = GL_UNSIGNED_SHORT;
            }
            else if (indexAccessor->component_type == cgltf_component_type_r_8u)
            {
                indexType = GL_UNSIGNED_BYTE;
            }
        }

        glBindVertexArray(0);

        app->ybotIndexCount = (GLsizei)indexAccessor->count;
        app->ybotIndexType = indexType;

        // Load Y-Bot texture
        const char *ybotTexturePath =
#if defined(__ANDROID__)
            "models/y-bot/y-bot.webp";
#else
            "assets/models/y-bot/y-bot.webp";
        // "assets/models/animated-plane/animated_plane.webp";
#endif
        app->ybotTextureID = createTexture(ybotTexturePath);

        // Assuming the model uses the first skin definition (ybotData->skins[0])
        if (ybotData->skins_count > 0)
        {
            cgltf_skin &skin = ybotData->skins[0];
            if (skin.inverse_bind_matrices)
            {
                cgltf_accessor *ibmAccessor = skin.inverse_bind_matrices;
                size_t numJointsInSkin = skin.joints_count;
                app->ybotInverseBindMatrices.resize(numJointsInSkin);

                // Access raw float data from the buffer view
                float *ibmData = (float *)((uint8_t *)ibmAccessor->buffer_view->buffer->data +
                                           ibmAccessor->buffer_view->offset +
                                           ibmAccessor->offset);

                for (size_t i = 0; i < numJointsInSkin; ++i)
                {
                    // Each matrix is 16 floats (4x4)
                    memcpy(&app->ybotInverseBindMatrices[i], &ibmData[i * 16], 16 * sizeof(float));
                }
            }
        }

        cgltf_free(ybotData);
        SDL_free(ybotGlbData);
    }
    else
    {
        SDL_Log("Failed to parse Y-Bot model: %s", ybotPath);
        return SDL_APP_FAILURE;
    }

    // Load Ozz Skeleton
    const char *skeletonPath =
#if defined(__ANDROID__)
        "models/y-bot/skeleton.ozz";
#else
        "assets/models/y-bot/skeleton.ozz";
#endif

    SDL_IOStream *skeletonIo = SDL_IOFromFile(skeletonPath, "rb");
    if (skeletonIo)
    {
        OzzSDLStream stream(skeletonIo);
        ozz::io::IArchive archive(&stream);
        if (archive.TestTag<ozz::animation::Skeleton>())
        {
            ozz::animation::Skeleton tempSkeleton;
            archive >> tempSkeleton;
            app->ozzSkeleton = std::move(tempSkeleton);
        }
        else
        {
            SDL_Log("CRITICAL: Archive tag mismatch for Skeleton!");
        }
    }
    else
    {
        SDL_Log("Failed to open ozz skeleton: %s", skeletonPath);
        return SDL_APP_FAILURE;
    }

    // Helper lambda to load an ozz animation
    auto loadAnimation = [](const char *path, ozz::animation::Animation &anim) -> bool {
        SDL_IOStream *animIo = SDL_IOFromFile(path, "rb");
        if (animIo)
        {
            OzzSDLStream stream(animIo);
            ozz::io::IArchive archive(&stream);
            archive >> anim;
            return true;
        }
        else
        {
            SDL_Log("Failed to open ozz animation: %s", path);
            return false;
        }
    };

#if defined(__ANDROID__)
    const char *idlePath = "models/y-bot/animation-idle.ozz";
    const char *walkPath = "models/y-bot/animation-walk.ozz";
    const char *runPath = "models/y-bot/animation-run.ozz";
#else
    const char *idlePath = "assets/models/y-bot/animation-idle.ozz";
    const char *walkPath = "assets/models/y-bot/animation-walk.ozz";
    const char *runPath = "assets/models/y-bot/animation-run.ozz";
#endif

    loadAnimation(idlePath, app->ozzIdleAnimation);
    loadAnimation(walkPath, app->ozzWalkAnimation);
    loadAnimation(runPath, app->ozzRunAnimation);

    // Initialize sampling context and timer
    app->ozzSamplingContext.Resize(app->ozzSkeleton.num_joints());
    app->ozzWalkAnimationTime = 0.0f;
    app->ozzRunAnimationTime = 0.0f;
    app->ozzModelMatrices.resize(app->ozzSkeleton.num_joints());

    // // Print animation details
    // SDL_Log("--- ozz::animation::Animation Details ---");
    // SDL_Log("Animation Duration: %.2f seconds", app->ozzAnimation.duration());
    // SDL_Log("Number of Tracks: %d", app->ozzAnimation.num_tracks());

    // for (size_t i = 0; i < app->ybotInverseBindMatrices.size(); ++i)
    // {
    //     mat4 &invBind = app->ybotInverseBindMatrices[i].m;
    //     SDL_Log("--- Inverse Bind Matrix for Joint ID %zu ---", i);
    //     SDL_Log("Col 0: [%.3f, %.3f, %.3f, %.3f]", invBind[0][0], invBind[0][1], invBind[0][2], invBind[0][3]);
    //     SDL_Log("Col 1: [%.3f, %.3f, %.3f, %.3f]", invBind[1][0], invBind[1][1], invBind[1][2], invBind[1][3]);
    //     SDL_Log("Col 2: [%.3f, %.3f, %.3f, %.3f]", invBind[2][0], invBind[2][1], invBind[2][2], invBind[2][3]);
    //     SDL_Log("Col 3 (Translation): [%.3f, %.3f, %.3f, %.3f]", invBind[3][0], invBind[3][1], invBind[3][2], invBind[3][3]);
    // }

    // Debug draw
    app->dd = b3DefaultDebugDraw();
    app->dd.DrawShapeFcn = drawShape;
    app->dd.drawShapes = true;
    app->dd.context = app;

    app->dd.drawingBounds = (b3AABB) {
        (b3Vec3) { -100.0f, -100.0f, -100.0f },
        (b3Vec3) { 100.0f, 100.0f, 100.0f }
    };

    // Box3D world
    b3WorldDef worldDef = b3DefaultWorldDef();
    worldDef.gravity = (b3Vec3) { 0.0f, -9.8f, 0.0f };
    worldDef.createDebugShape = createDebugShape;
    worldDef.destroyDebugShape = destroyDebugShape;
    app->worldId = b3CreateWorld(&worldDef);

    // Platform rotation
    // Convert 20 degrees to radians
    float angleInRadians = 20.0f * (3.14f / 180.0f);
    // Define the axis of rotation (Z-axis)
    b3Vec3 axis = { 0.f, 0.f, 1.f };
    // Create the quaternion
    b3Quat rotation = b3MakeQuatFromAxisAngle(axis, angleInRadians);

    // Create a platform
    b3BodyDef platformBodyDef = b3DefaultBodyDef();
    platformBodyDef.position = (b3Vec3) { 5.f, -2.f, -1.f };
    platformBodyDef.rotation = rotation;
    b3BodyId platformId = b3CreateBody(app->worldId, &platformBodyDef);
    b3BoxHull platformBox = b3MakeBoxHull(3.f, 0.1f, 1.f);
    b3ShapeDef platformShapeDef = b3DefaultShapeDef();
    b3CreateHullShape(platformId, &platformShapeDef, &platformBox.base);

    // Create a ground
    b3BodyDef groundBodyDef = b3DefaultBodyDef();
    groundBodyDef.position = (b3Vec3) { 2.f, -3.f, 0.f };
    b3BodyId groundId = b3CreateBody(app->worldId, &groundBodyDef);
    b3BoxHull groundBox = b3MakeBoxHull(7.f, 0.2f, 5.f);
    b3ShapeDef groundShapeDef = b3DefaultShapeDef();
    b3CreateHullShape(groundId, &groundShapeDef, &groundBox.base);

    // Create a dynamic falling box
    b3BodyDef boxBodyDef = b3DefaultBodyDef();
    boxBodyDef.type = b3_dynamicBody;                 // This tells Box3D it should fall
    boxBodyDef.position = (b3Vec3) { 0.f, 5.f, 0.f }; // Start 5 units above
    b3BodyId boxId = b3CreateBody(app->worldId, &boxBodyDef);
    b3BoxHull boxHull = b3MakeBoxHull(0.5f, 0.5f, 0.5f);
    b3ShapeDef boxShapeDef = b3DefaultShapeDef();
    // Setting density makes the object have mass so gravity affects it
    boxShapeDef.density = 1.0f;
    b3CreateHullShape(boxId, &boxShapeDef, &boxHull.base);

    // Create a dynamic falling sphere
    b3BodyDef sphereBodyDef = b3DefaultBodyDef();
    sphereBodyDef.type = b3_dynamicBody;                    // Dynamic body so gravity affects it
    sphereBodyDef.position = (b3Vec3) { 0.5f, 8.0f, 0.0f }; // Start slightly offset and higher up
    b3BodyId sphereId = b3CreateBody(app->worldId, &sphereBodyDef);

    b3Sphere sphere = { .radius = 0.5f };
    b3ShapeDef sphereShapeDef = b3DefaultShapeDef();
    sphereShapeDef.density = 1.0f; // Density gives it mass
    b3CreateSphereShape(sphereId, &sphereShapeDef, &sphere);

    // Create a dynamic falling capsule
    b3BodyDef capsuleBodyDef = b3DefaultBodyDef();
    capsuleBodyDef.type = b3_dynamicBody;                     // Dynamic body so gravity affects it
    capsuleBodyDef.position = (b3Vec3) { -0.5f, 1.0f, 0.0f }; // Start offset and above the box/sphere

    // Lock all rotation axes so the capsule stays upright
    b3MotionLocks locks = { 0 };
    locks.angularX = true; // Prevents tipping forward/backward
    locks.angularY = true; // Prevents spinning sideways
    locks.angularZ = true; // Prevents tipping left/right
    capsuleBodyDef.motionLocks = locks;

    app->capsuleId = b3CreateBody(app->worldId, &capsuleBodyDef);

    // Box3D capsule defined by two center points of its hemispherical caps and a radius
    b3Capsule capsule = {
        .center1 = { 0.0f, -0.7f, 0.0f }, // Bottom center
        .center2 = { 0.0f, 0.7f, 0.0f },  // Top center
        .radius = 0.5f
    };
    b3ShapeDef capsuleShapeDef = b3DefaultShapeDef();
    capsuleShapeDef.density = 1.0f;
    capsuleShapeDef.baseMaterial.friction = 0.5f;
    capsuleShapeDef.baseMaterial.restitution = 0.0f; // Prevent bouncing
    b3CreateCapsuleShape(app->capsuleId, &capsuleShapeDef, &capsule);

    app->showRobot = true;
    app->showPhysicsDebug = true;

    // Store mobile status in the App struct for UI rendering decisions
    app->isMobile = isMobilePlatform();

    return SDL_APP_CONTINUE;
}

// This function runs when a new event (mouse input, keypresses, etc) occurs
SDL_AppResult SDL_AppEvent(void *appState, SDL_Event *event)
{
    App *app = (App *)appState;

    // Forward events to ImGui SDL3 backend
    ImGui_ImplSDL3_ProcessEvent(event);

    switch (event->type)
    {
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED:
        {
            // Update projection matrices
            updateProjectionAndMVP(app);   // Updates 2D text
            update3DProjectionAndMVP(app); // Updates 3D cube

            // Recalculate scale factor for mobile browser orientation shifts
            float newScale = SDL_GetWindowDisplayScale(app->window);
            if (newScale > 0.0f)
            {
                app->scale_factor = newScale;
                if (app->isMobile)
                    app->scale_factor *= 1.0f;
            }

            // Flag orientation/size update so ImGui can reposition
            app->needsReposition = true;

            break;
        }

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        {
            // Ignore key repeats to avoid extra processing overhead
            if (event->key.repeat)
                break;

            bool isPressed = (event->type == SDL_EVENT_KEY_DOWN);

            // Handle directional key inputs
            switch (event->key.scancode)
            {
                case SDL_SCANCODE_W:
                case SDL_SCANCODE_UP:
                    app->moveUp = isPressed;
                    break;

                case SDL_SCANCODE_S:
                case SDL_SCANCODE_DOWN:
                    app->moveDown = isPressed;
                    break;

                case SDL_SCANCODE_A:
                case SDL_SCANCODE_LEFT:
                    app->moveLeft = isPressed;
                    break;

                case SDL_SCANCODE_D:
                case SDL_SCANCODE_RIGHT:
                    app->moveRight = isPressed;
                    break;

                default:
                    break;
            }
            break;
        }

        case SDL_EVENT_QUIT:
            return SDL_APP_SUCCESS;
        default:
            break;
    }

    return SDL_APP_CONTINUE;
}

// This function runs once per frame, and is the heart of the program
SDL_AppResult SDL_AppIterate(void *appState)
{
    App *app = (App *)appState;

    // Get screen dimensions in pixels
    int windowW, windowH;
    SDL_GetWindowSizeInPixels(app->window, &windowW, &windowH);

    // Query and update scale factor dynamically
    float currentScale = SDL_GetWindowDisplayScale(app->window);
    if (currentScale > 0.0f)
    {
        app->scale_factor = currentScale;
    }
    else if (app->scale_factor <= 0.0f)
    {
        app->scale_factor = 1.0f;
    }

    // Track scale factor and window size changes to trigger repositioning on resize
    static float lastScale = 0.0f;
    static int lastW = 0, lastH = 0;

    bool scaleChanged = (app->scale_factor != lastScale);
    bool sizeChanged = (windowW != lastW || windowH != lastH);

    lastScale = app->scale_factor;
    lastW = windowW;
    lastH = windowH;

    // Start ImGui Frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // Sync ImGui IO display size directly with SDL backbuffer dimensions
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)windowW, (float)windowH);

    // Force repositioning on orientation shifts, window resizes, or explicit flags
    ImGuiCond cond = (app->needsReposition || scaleChanged || sizeChanged)
                         ? ImGuiCond_Always
                         : ImGuiCond_FirstUseEver;

    // Clear reposition trigger flag
    app->needsReposition = false;

    // Position window based on device mode and orientation
    if (app->isMobile)
    {
        bool isPortrait = (windowH > windowW);

        if (isPortrait)
        {
            // Portrait position: top margin to clear status bar/notch
            ImGui::SetNextWindowPos(ImVec2(20.0f * app->scale_factor, 70.0f * app->scale_factor), ImGuiCond_Always);
        }
        else
        {
            // Landscape position: tighter top/left margins
            ImGui::SetNextWindowPos(ImVec2(50.0f * app->scale_factor, 20.0f * app->scale_factor), ImGuiCond_Always);
        }
    }
    else
    {
        ImGui::SetNextWindowPos(ImVec2(20.0f * app->scale_factor, 20.0f * app->scale_factor), cond);
    }

    // Set window size on first boot
    ImGui::SetNextWindowSize(ImVec2(270.0f * app->scale_factor, 220.0f * app->scale_factor), ImGuiCond_FirstUseEver);

    // Render ImGui Window
    ImGui::Begin("Hello/Привет");
    ImGui::Checkbox("Show Debug Skeleton", &app->showDebugSkeleton);
    ImGui::Checkbox("Show Robot", &app->showRobot);
    ImGui::Checkbox("Show Physics Debug", &app->showPhysicsDebug);

    // ImGui::Text("--- DIAGNOSTICS ---");
    // ImGui::Text("isMobile: %s", app->isMobile ? "TRUE" : "FALSE");
    // ImGui::Text("scale_factor: %.2f", app->scale_factor);
    // ImGui::Separator();

    // ImGui::Text("Target Pos Set: (20.0, 70.0)");
    // ImGui::Text("Actual Window Pos: (%.1f, %.1f)", actualPos.x, actualPos.y);
    // ImGui::Separator();

    // ImGui::Text("ImGui DisplaySize: %.0fx%.0f", io.DisplaySize.x, io.DisplaySize.y);
    // ImGui::Text("SDL Window Pixels: %dx%d", windowW, windowH);

    static float blendFactor = 0.0f;
    ImGui::Text("Walk <-> Run");
    ImGui::SliderFloat("##WalkRunSlider", &blendFactor, 0.0f, 1.0f);
    ImGui::End();

    // On-Screen Touch Joystick for Mobile
    if (app->isMobile)
    {
        // Get window size locally to avoid scoping issues
        int windowW, windowH;
        SDL_GetWindowSizeInPixels(app->window, &windowW, &windowH);

        float radius = 60.0f * app->scale_factor;
        float handleRadius = 25.0f * app->scale_factor; // Knob size
        float joystickPadding = 20.0f * app->scale_factor;

        // Bottom-left corner in ImGui screen space
        ImVec2 center(joystickPadding + radius + handleRadius, (float)windowH - joystickPadding - radius - handleRadius);

        // Include handleRadius in total window size
        float totalHalfSize = radius + handleRadius;
        ImGui::SetNextWindowPos(ImVec2(center.x - totalHalfSize, center.y - totalHalfSize));
        ImGui::SetNextWindowSize(ImVec2(totalHalfSize * 2.0f, totalHalfSize * 2.0f));

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_NoBackground |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoMove;

        ImGui::Begin("VirtualJoystick", nullptr, flags);

        ImDrawList *draw_list = ImGui::GetWindowDrawList();

        // Base Outer Ring
        draw_list->AddCircleFilled(center, radius, IM_COL32(100, 100, 100, 120));
        draw_list->AddCircle(center, radius, IM_COL32(255, 255, 255, 200), 32, 2.0f);

        // Invisible Button sized to the entire clickable window space
        ImGui::InvisibleButton("JoystickBtn", ImVec2(totalHalfSize * 2.0f, totalHalfSize * 2.0f));

        ImVec2 handlePos = center; // Default to center position

        if (ImGui::IsItemActive()) // True while holding/dragging
        {
            ImVec2 touchPos = ImGui::GetIO().MousePos;
            float dx = touchPos.x - center.x;
            float dy = touchPos.y - center.y;
            float dist = sqrtf(dx * dx + dy * dy);

            if (dist > 0.0f)
            {
                // Clamp handle movement inside the outer radius
                float clampDist = (dist > radius) ? radius : dist;
                handlePos.x = center.x + (dx / dist) * clampDist;
                handlePos.y = center.y + (dy / dist) * clampDist;

                // Map touch offset to -1.0 to +1.0 movement axes
                // In screen space +Y is DOWN, but in 3D world space +Z is BACKWARD.
                // dx maps to moveX, dy maps to moveZ
                app->moveX_joystick = (handlePos.x - center.x) / radius;
                app->moveZ_joystick = (handlePos.y - center.y) / radius;
            }
        }
        else
        {
            // Reset joystick inputs when touch is released
            app->moveX_joystick = 0.0f;
            app->moveZ_joystick = 0.0f;
        }

        // Render Center Thumbstick Knob (now fits completely inside the window canvas)
        draw_list->AddCircleFilled(handlePos, handleRadius, IM_COL32(255, 255, 255, 220));

        ImGui::End();
    }

    // Calculate horizontal direction combining keyboard and touch joystick
    float moveX = 0.0f;
    float moveZ = 0.0f;
    float speed = 5.0f; // Movement velocity magnitude

    // Keyboard Inputs (D-Pad style)
    if (app->moveUp)
        moveZ -= 1.0f;
    if (app->moveDown)
        moveZ += 1.0f;
    if (app->moveLeft)
        moveX -= 1.0f;
    if (app->moveRight)
        moveX += 1.0f;

    // Add Touch Joystick Inputs (Analog style)
    if (app->isMobile)
    {
        moveX += app->moveX_joystick;
        moveZ += app->moveZ_joystick;
    }

    // Clamp / Normalize movement vector to max magnitude of 1.0
    float len = sqrtf(moveX * moveX + moveZ * moveZ);
    if (len > 1.0f)
    {
        moveX = (moveX / len);
        moveZ = (moveZ / len);
    }

    // Scale by velocity magnitude
    moveX *= speed;
    moveZ *= speed;

    // Preserve gravity's Y component while applying X/Z velocity
    b3Vec3 currentVel = b3Body_GetLinearVelocity(app->capsuleId);
    b3Vec3 newVel = { moveX, currentVel.y, moveZ };

    // Apply velocity to Box3D capsule
    b3Body_SetLinearVelocity(app->capsuleId, newVel);

    // Ozz Animation Update
    static Uint64 last_ticks = SDL_GetTicks();
    Uint64 current_ticks = SDL_GetTicks();
    float delta_time = (float)(current_ticks - last_ticks) / 1000.0f;
    last_ticks = current_ticks;

    b3World_Step(app->worldId, 1.0f / 60.0f, 5);

    // Synchronized Blending Parameters & Speeds
    float walkDuration = app->ozzWalkAnimation.duration();
    float runDuration = app->ozzRunAnimation.duration();

    // Calculate weights for the two layers based on blendFactor (0.0 = Walk, 1.0 = Run)
    float walkWeight = 1.0f - blendFactor;
    float runWeight = blendFactor;

    // Interpolate animation durations to find the unified loop cycle duration
    float loopDuration = (walkDuration * walkWeight) + (runDuration * runWeight);

    // Prevent division by zero
    float invLoopDuration = (loopDuration > 0.0f) ? (1.0f / loopDuration) : 0.0f;

    // Adjust playback speeds so both cycles lock in step
    float walkSpeed = walkDuration * invLoopDuration;
    float runSpeed = runDuration * invLoopDuration;

    // Advance time using the synchronized speeds
    app->ozzWalkAnimationTime += delta_time * walkSpeed;
    if (walkDuration > 0.0f)
    {
        app->ozzWalkAnimationTime = fmod(app->ozzWalkAnimationTime, walkDuration);
        if (app->ozzWalkAnimationTime < 0.0f)
            app->ozzWalkAnimationTime += walkDuration;
    }
    else
    {
        app->ozzWalkAnimationTime = 0.0f;
    }

    app->ozzRunAnimationTime += delta_time * runSpeed;
    if (runDuration > 0.0f)
    {
        app->ozzRunAnimationTime = fmod(app->ozzRunAnimationTime, runDuration);
        if (app->ozzRunAnimationTime < 0.0f)
            app->ozzRunAnimationTime += runDuration;
    }
    else
    {
        app->ozzRunAnimationTime = 0.0f;
    }

    // Setup and run sampling job for Walk
    ozz::animation::SamplingJob walk_sampling_job;
    walk_sampling_job.animation = &app->ozzWalkAnimation;
    walk_sampling_job.ratio = walkDuration > 0.0f ? (app->ozzWalkAnimationTime / walkDuration) : 0.0f;
    walk_sampling_job.context = &app->ozzSamplingContext;

    ozz::vector<ozz::math::SoaTransform> walkLocals;
    walkLocals.resize(app->ozzSkeleton.num_soa_joints());
    walk_sampling_job.output = ozz::make_span(walkLocals);

    if (!walk_sampling_job.Run())
    {
        SDL_Log("Failed to sample ozz walk animation.");
        return SDL_APP_CONTINUE;
    }

    // Setup and run sampling job for Run
    ozz::animation::SamplingJob::Context runSamplingContext;
    runSamplingContext.Resize(app->ozzSkeleton.num_joints());

    ozz::animation::SamplingJob run_sampling_job;
    run_sampling_job.animation = &app->ozzRunAnimation;
    run_sampling_job.ratio = runDuration > 0.0f ? (app->ozzRunAnimationTime / runDuration) : 0.0f;
    run_sampling_job.context = &runSamplingContext;

    ozz::vector<ozz::math::SoaTransform> runLocals;
    runLocals.resize(app->ozzSkeleton.num_soa_joints());
    run_sampling_job.output = ozz::make_span(runLocals);

    if (!run_sampling_job.Run())
    {
        SDL_Log("Failed to sample ozz run animation.");
        return SDL_APP_CONTINUE;
    }

    // Blend the two local transform sets together using BlendingJob
    ozz::animation::BlendingJob::Layer layers[2];
    layers[0].transform = ozz::make_span(walkLocals);
    layers[0].weight = walkWeight; // Use the synchronized weight

    layers[1].transform = ozz::make_span(runLocals);
    layers[1].weight = runWeight; // Use the synchronized weight

    ozz::vector<ozz::math::SoaTransform> blendedLocals;
    blendedLocals.resize(app->ozzSkeleton.num_soa_joints());

    ozz::animation::BlendingJob blend_job;
    blend_job.rest_pose = app->ozzSkeleton.joint_rest_poses();
    blend_job.layers = ozz::make_span(layers);
    blend_job.output = ozz::make_span(blendedLocals);

    // -Set threshold to avoid strict normalization assertion
    // failures on slight floating point drift.
    // Default is usually 0.01f or similar, loosening it or making sure it's explicitly set helps
    blend_job.threshold = 0.1f;

    if (!blend_job.Run())
    {
        SDL_Log("Failed to run ozz BlendingJob.");
    }

    // Convert blended local transforms to model-space matrices
    ozz::animation::LocalToModelJob conv_job;
    conv_job.skeleton = &app->ozzSkeleton;
    conv_job.input = ozz::make_span(blendedLocals);
    conv_job.output = ozz::make_span(app->ozzModelMatrices);

    if (!conv_job.Run())
    {
        SDL_Log("Failed to run local-to-model animation job.");
    }

    ImGui::Render();

    // Enable depth testing for 3D rendering
    glEnable(GL_DEPTH_TEST);

    glViewport(0, 0, (int)app->io->DisplaySize.x, (int)app->io->DisplaySize.y);

    glClearColor(0.2f, 0.2f, 0.2f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Only draw physics debug lines when enabled
    if (app->showPhysicsDebug)
    {
        b3World_Draw(app->worldId, &app->dd, B3_DEFAULT_MASK_BITS);
    }

    // Draw 3D Cube with texture shader
    glUseProgram(app->shaderProgram);

    // Model Matrix (Cube transformations: position, rotation, scale)
    mat4 cubeModelMatrix;
    glm_mat4_identity(cubeModelMatrix);
    vec3 cubeTranslation = { -5.f, 0.f, 0.f };
    glm_translate(cubeModelMatrix, cubeTranslation);
    vec3 cubeScale = { 1.2f, 1.2f, 1.2f };
    glm_scale(cubeModelMatrix, cubeScale);
    mat4 cubeMvpMatrix;
    glm_mat4_mul(app->projView3D, cubeModelMatrix, cubeMvpMatrix);
    glUniformMatrix4fv(app->uMvpMatrixLocation, 1, GL_FALSE, (const GLfloat *)cubeMvpMatrix);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, app->cubeTextureID);
    glBindVertexArray(app->cubeVao);
    glDrawElements(GL_TRIANGLES, app->cubeIndexCount, app->cubeIndexType, 0);
    glBindVertexArray(0);

    // Fetch Position and Quaternion Rotation from Box3D
    // b3Vec3 capsulePos = b3Body_GetPosition(app->capsuleId);
    // b3Quat capsuleRot = b3Body_GetRotation(app->capsuleId);

    vec3 yBotTranslation = { 0.7f, -0.23f, 0.0f };
    vec3 yBotScale = { 5.0f, 5.0f, 5.0f };

    // Calculate View-Projection matrix for the skeleton
    mat4 skeletonMvp;
    mat4 skeletonModel;
    glm_mat4_identity(skeletonModel);

    // Scale the model uniformly or non-uniformly (X, Y, Z scale factors)
    glm_scale(skeletonModel, yBotScale);

    // Move the model to a specific 3D position (X, Y, Z)
    glm_translate(skeletonModel, yBotTranslation);

    glm_mat4_mul(app->projView3D, skeletonModel, skeletonMvp);

    // Render Skeleton Bones (Only if flag is enabled)
    if (app->showDebugSkeleton)
    {
        glUseProgram(app->lineShaderProgram);

        glUniformMatrix4fv(app->uLineMvpMatrixLocation, 1, GL_FALSE, (const GLfloat *)skeletonMvp);

        // Build line vertices from ozzModelMatrices...
        std::vector<float> lineVertices;
        for (int i = 0; i < app->ozzSkeleton.num_joints(); ++i)
        {
            int parentIdx = app->ozzSkeleton.joint_parents()[i];
            if (parentIdx == ozz::animation::Skeleton::kNoParent)
            {
                continue;
            }

            const ozz::math::Float4x4 &currentMat = app->ozzModelMatrices[i];
            const ozz::math::Float4x4 &parentMat = app->ozzModelMatrices[parentIdx];

            // Store 3 components (x, y, z) of the translation column (cols[3]) directly into floats
            float p[3], c[3];
            ozz::math::Store3PtrU(parentMat.cols[3], p);
            ozz::math::Store3PtrU(currentMat.cols[3], c);

            // Push parent position
            lineVertices.push_back(p[0]);
            lineVertices.push_back(p[1]);
            lineVertices.push_back(p[2]);

            // Push child position
            lineVertices.push_back(c[0]);
            lineVertices.push_back(c[1]);
            lineVertices.push_back(c[2]);
        }

        glUniform4f(app->uLineColorLocation, 0.0f, 1.0f, 0.0f, 1.0f);

        // Upload line data to GPU and render
        if (!lineVertices.empty())
        {
            // Each line has 2 points, and each point has 3 floats (x, y, z) -> 6 floats per line segment
            // size_t totalFloats = lineVertices.size();
            // size_t numSegments = totalFloats / 6;
            // SDL_Log("Rendering %zu line segments (%zu floats total)", numSegments, totalFloats);

            glBindBuffer(GL_ARRAY_BUFFER, app->lineVbo);
            // Use glBufferData or glBufferSubData to update vertices each frame
            glBufferData(GL_ARRAY_BUFFER, lineVertices.size() * sizeof(float), lineVertices.data(), GL_DYNAMIC_DRAW);

            glBindVertexArray(app->lineVao);
            glDrawArrays(GL_LINES, 0, (GLsizei)(lineVertices.size() / 3));
            glBindVertexArray(0);
        }

        glEnable(GL_DEPTH_TEST);
    }

    // Render Skinned Y-Bot Model (Only if flag is enabled)
    if (app->showRobot)
    {
        glUseProgram(app->skinningShaderProgram);

        // Use the same 3D projection and view matrices as your cube/skeleton
        mat4 ybotModel;
        glm_mat4_identity(ybotModel);

        // Position and scale Y-Bot appropriately in your scene
        glm_scale(ybotModel, yBotScale);
        glm_translate(ybotModel, yBotTranslation);

        mat4 ybotMvp;
        glm_mat4_mul(app->projView3D, ybotModel, ybotMvp);
        glUniformMatrix4fv(app->uSkinningMvpMatrixLocation, 1, GL_FALSE, (const GLfloat *)ybotMvp);

        // const int activeJointIndices[] = { 2, 3 };
        // const int numActiveJoints = 2;

        // const int startIndex = 2; // mixamorig:Hips
        // const int numActiveJoints = 65;

        // clang-format off
        const int activeJointIndices[] = {
            2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 
            22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 
            40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 
            58, 59, 60, 61, 62, 63, 64, 65, 66
        };
        // clang-format on

        const int numActiveJoints = 65;

        std::vector<float> jointMatricesData(numActiveJoints * 16);

        for (int idx = 0; idx < numActiveJoints; ++idx)
        {
            int i = activeJointIndices[idx];
            const ozz::math::Float4x4 &m = app->ozzModelMatrices[i];

            mat4 ozzMat;
            memcpy(&ozzMat[0][0], &m.cols[0], 16 * sizeof(float));

            // // --- PRINT OZZMAT ---
            // SDL_Log("--- ozzMat for Active Joint Index %d (Ozz ID %d) ---", idx, i);
            // SDL_Log("Col 0: [%.3f, %.3f, %.3f, %.3f]", ozzMat[0][0], ozzMat[0][1], ozzMat[0][2], ozzMat[0][3]);
            // SDL_Log("Col 1: [%.3f, %.3f, %.3f, %.3f]", ozzMat[1][0], ozzMat[1][1], ozzMat[1][2], ozzMat[1][3]);
            // SDL_Log("Col 2: [%.3f, %.3f, %.3f, %.3f]", ozzMat[2][0], ozzMat[2][1], ozzMat[2][2], ozzMat[2][3]);
            // SDL_Log("Col 3 (Translation): [%.3f, %.3f, %.3f, %.3f]", ozzMat[3][0], ozzMat[3][1], ozzMat[3][2], ozzMat[3][3]);
            // // --------------------

            // // --- PRINT INVERSE BIND MATRIX ---
            // if (i < app->ybotInverseBindMatrices.size())
            // {
            //     mat4 &invBind = app->ybotInverseBindMatrices[i].m;
            //     SDL_Log("--- Inverse Bind Matrix for Active Joint Index %d (Ozz ID %d) ---", idx, i);
            //     SDL_Log("Col 0: [%.3f, %.3f, %.3f, %.3f]", invBind[0][0], invBind[0][1], invBind[0][2], invBind[0][3]);
            //     SDL_Log("Col 1: [%.3f, %.3f, %.3f, %.3f]", invBind[1][0], invBind[1][1], invBind[1][2], invBind[1][3]);
            //     SDL_Log("Col 2: [%.3f, %.3f, %.3f, %.3f]", invBind[2][0], invBind[2][1], invBind[2][2], invBind[2][3]);
            //     SDL_Log("Col 3 (Translation): [%.3f, %.3f, %.3f, %.3f]", invBind[3][0], invBind[3][1], invBind[3][2], invBind[3][3]);
            // }
            // // ---------------------------------

            mat4 finalJointMat;

            // DEBUG: Let's check if the inverse bind matrix is causing the offset
            if (idx < app->ybotInverseBindMatrices.size()) // Only apply to Bone, skip or check Bone.001
            {
                glm_mat4_mul(ozzMat, app->ybotInverseBindMatrices[idx].m, finalJointMat);
            }
            else
            {
                // If Bone.001 has a bad inverse bind matrix offset from the exporter,
                // try copying ozzMat directly without the inverse bind matrix:
                glm_mat4_copy(ozzMat, finalJointMat);
            }

            // --- DEBUG PRINTING ---
            // 1. Extract Position (Translation is stored in column 3: [3][0], [3][1], [3][2])
            // float posX = finalJointMat[3][0];
            // float posY = finalJointMat[3][1];
            // float posZ = finalJointMat[3][2];

            // 2. Extract Rotation (as a Quaternion using cglm)
            // versor quat;
            // glm_mat4_quat(finalJointMat, quat); // quat is [x, y, z, w]

            // SDL_Log("Active Joint Index %d (Ozz ID %d):", idx, i);
            // SDL_Log("  Position -> X: %.3f, Y: %.3f, Z: %.3f", posX, posY, posZ);
            // SDL_Log("  Rotation -> X: %.3f, Y: %.3f, Z: %.3f, W: %.3f", quat[0], quat[1], quat[2], quat[3]);
            // ----------------------

            memcpy(&jointMatricesData[idx * 16], &finalJointMat[0][0], 16 * sizeof(float));
        }

        glUniformMatrix4fv(app->uJointMatricesLocation, numActiveJoints, GL_FALSE, jointMatricesData.data());

        // Bind texture, VAO, and draw
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, app->ybotTextureID);
        glBindVertexArray(app->ybotVao);
        glDrawElements(GL_TRIANGLES, app->ybotIndexCount, app->ybotIndexType, 0);
        glBindVertexArray(0);
    }

    glDisable(GL_DEPTH_TEST);

    // Draw 2D text
    // Enable 2D Shader & Text Texture
    glUseProgram(app->shaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, app->textTextureID);
    glBindVertexArray(app->vao);

    // Calculate Top-Right Position
    // X = Right edge minus text width minus padding
    // Y = Top edge minus text height minus padding (assuming Y=0 is bottom-left)
    // float posX = (float)windowW - (textW * 0.5f) - padding;
    // float posY = (float)windowH - (textH * 0.5f) - padding;

    // X = Right edge: Window width minus half text width minus padding
    // float posX = (float)windowW - (textW * 0.5f) - padding;
    // Y = Bottom edge: Zero plus half text height plus padding
    // float posY = (textH * 0.5f) + padding;

    float textW = (float)app->textW;
    float textH = (float)app->textH;
    float posX, posY;

    // Account for display density scaling if needed
    float padding = 20.0f * app->scale_factor;

    // Check if running on Mobile OR WebAssembly/Wasm
    #if defined(__EMSCRIPTEN__)
    bool isMobileOrWasm = true;
    #else
    bool isMobileOrWasm = app->isMobile;
    #endif

    if (isMobileOrWasm)
    {
        // Mobile / Wasm: Bottom-Right Corner
        // X = Window width minus half text width minus padding
        posX = (float)windowW - (textW * 0.5f) - padding;
        
        // Y = Bottom edge (Y=0) PLUS half text height plus padding
        posY = (textH * 0.5f) + padding;
    }
    else
    {
        // Native Desktop (Windows, Linux, macOS): Top-Right Corner
        // X = Window width minus half text width minus padding
        posX = (float)windowW - (textW * 0.5f) - padding;
        
        // Y = Top edge (Y=windowH) MINUS half text height minus padding
        posY = (float)windowH - (textH * 0.5f) - padding;
    }

    vec3 textTranslation = { posX, posY, 0.0f };

    mat4 textModelMatrix;
    glm_mat4_identity(textModelMatrix);

    // Position in world/screen space (applied LAST to vertices)
    glm_translate(textModelMatrix, textTranslation);

    // Scale to width/height dimensions (applied FIRST to vertices)
    // vec3 scaleFactors = { (float)app->textW, (float)app->textH, 1.0f };
    vec3 scaleFactors = { textW, textH, 1.0f };
    glm_scale(textModelMatrix, scaleFactors);

    mat4 textMvpMatrix;
    glm_mat4_mul(app->projView2D, textModelMatrix, textMvpMatrix);
    glUniformMatrix4fv(app->uMvpMatrixLocation, 1, GL_FALSE, (const GLfloat *)textMvpMatrix);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    // Update the screen
    SDL_GL_SwapWindow(app->window);
    return SDL_APP_CONTINUE;
}

// This function runs once at shutdown
void SDL_AppQuit(void *appState, SDL_AppResult result)
{
    App *app = (App *)appState;
    if (!app)
        return;

    // Clean up OpenGL resources
    glDeleteProgram(app->shaderProgram);
    glDeleteVertexArrays(1, &app->vao);
    glDeleteBuffers(1, &app->vbo);
    glDeleteTextures(1, &app->textTextureID);

    glDeleteVertexArrays(1, &app->cubeVao);
    glDeleteBuffers(1, &app->cubeVbo);
    glDeleteBuffers(1, &app->cubeUvVbo);
    glDeleteBuffers(1, &app->cubeEbo);
    glDeleteTextures(1, &app->cubeTextureID);

    glDeleteProgram(app->lineShaderProgram);
    glDeleteVertexArrays(1, &app->lineVao);
    glDeleteBuffers(1, &app->lineVbo);

    // Clean up Y-Bot and Skinning Resources
    glDeleteProgram(app->skinningShaderProgram);
    glDeleteVertexArrays(1, &app->ybotVao);
    glDeleteBuffers(1, &app->ybotVbo);
    glDeleteBuffers(1, &app->ybotUvVbo);
    glDeleteBuffers(1, &app->ybotJointVbo);
    glDeleteBuffers(1, &app->ybotWeightVbo);
    glDeleteBuffers(1, &app->ybotEbo);
    glDeleteTextures(1, &app->ybotTextureID);

    // Clean up Box3D physics world
    b3DestroyWorld(app->worldId);

    // Clean up SDL_ttf font resources (before calling TTF_Quit)
    if (app->font)
        TTF_CloseFont(app->font);

    // Destroy SDL Window and GL Context
    SDL_GL_DestroyContext(app->glContext);
    SDL_DestroyWindow(app->window);

    // Cleanup Contexts
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    // Quit subsystems in reverse order of initialization
    TTF_Quit();
    SDL_Quit();

    // Finally, free the app state memory
    delete app;
}
