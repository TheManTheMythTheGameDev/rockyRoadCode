/*******************************************************************************************
*
*   raylib [core] example - Basic 3d example
*
*   Welcome to raylib!
*
*   To compile example, just press F5.
*   Note that compiled executable is placed in the same folder as .c file
*
*   You can find all basic examples on C:\raylib\raylib\examples folder or
*   raylib official webpage: www.raylib.com
*
*   Enjoy using raylib. :)
*
*   This example has been created using raylib 1.0 (www.raylib.com)
*   raylib is licensed under an unmodified zlib/libpng license (View raylib.h for details)
*
*   Copyright (c) 2013-2020 Ramon Santamaria (@raysan5)
*
********************************************************************************************/

#define RL_VECTOR2_TYPE
#define PHYSAC_IMPLEMENTATION
#include "raylib.h"
#include "rlgl.h"
#include "FPCamera.h"
#include "rlpbr.h"
#include "physac.h"
#include "stdio.h"
#define RAYGUI_IMPLEMENTATION
#include "extras/raygui.h"

#define LETTER_BOUNDRY_SIZE 0.25f
#define TEXT_MAX_LAYERS 32
#define LETTER_BOUNDRY_COLOR VIOLET

bool SHOW_LETTER_BOUNDRY = false;
bool SHOW_TEXT_BOUNDRY = true;

bool getCurrentGround(Vector3 pos, Model groundArr);
void DrawTextCodepoint3D(Font font, int codepoint, Vector3 position, float fontSize, bool backface, Color tint);
void DrawText3D(Font font, const char *text, Vector3 position, float fontSize, float fontSpacing, float lineSpacing, bool backface, Color tint);
static TextureCubemap GenTextureCubemap(Shader shader, Texture2D panorama, int size, int format);
static float GetSpeedForAxis(FPCamera *camera, CameraControls axis, float speed);

int main()
{
    // Initialization
    //--------------------------------------------------------------------------------------
    const int screenWidth = 800;
    const int screenHeight = 450;

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth, screenHeight, "Rocky Road");
    SetWindowIcon(LoadImage("icon.png"));

    FPCamera cam;
    InitFPCamera(&cam, 60, Vector3Zero());
    cam.CameraPosition = Vector3Zero();
    cam.ViewCamera.target = (Vector3) {15, 0, 0};
    SetCameraMode(cam.ViewCamera, CAMERA_ORBITAL);

    typedef enum GameState
    {
        Start = 0,
        Intro,
        Playing,
        Respawn,
        Finish
    } GameState;

    typedef struct Level
    {
        Model *groundArr;
        int elementAmount;
    } Level;

    GameState currentState = Intro;
    int currentLevel = 0;

    Model playerModel = LoadModel("player.glb");
    int playerAnimsCount;
    ModelAnimation *playerAni = LoadModelAnimations("player.glb", &playerAnimsCount);
    UpdateModelAnimation(playerModel, *playerAni, 10);
    Texture playerAlbedo = LoadTexture("playerAlbedo.png");
    playerModel.materials[0].maps[MATERIAL_MAP_ALBEDO].texture = playerAlbedo;
    SetTextureFilter(playerAlbedo, TEXTURE_FILTER_ANISOTROPIC_16X);

    Mesh cube = GenMeshCube(1.0f, 1.0f, 1.0f);
    Model skybox = LoadModelFromMesh(cube);
    skybox.materials[0].shader = LoadShader(TextFormat("skybox.vs"),
                                            TextFormat("skybox.fs"));
    int cubemap = MATERIAL_MAP_CUBEMAP;
    int numberone = 1;
    bool useHDR = false;
    SetShaderValue(skybox.materials[0].shader, GetShaderLocation(skybox.materials[0].shader, "environmentMap"), (int[1]){MATERIAL_MAP_CUBEMAP}, SHADER_UNIFORM_INT);
    SetShaderValue(skybox.materials[0].shader, GetShaderLocation(skybox.materials[0].shader, "doGamma"), (int[1]){useHDR ? 1 : 0}, SHADER_UNIFORM_INT);
    SetShaderValue(skybox.materials[0].shader, GetShaderLocation(skybox.materials[0].shader, "vflipped"), (int[1]){useHDR ? 1 : 0}, SHADER_UNIFORM_INT);

    Shader shdrCubemap = LoadShader(TextFormat("cubemap.vs"),
                                    TextFormat("cubemap.fs"));

    SetShaderValue(shdrCubemap, GetShaderLocation(shdrCubemap, "equirectangularMap"), (int[1]){0}, SHADER_UNIFORM_INT);

    char skyboxFileName[256] = {0};

    Texture2D panorama;

    if (useHDR)
    {
        TextCopy(skyboxFileName, "resources/dresden_square_2k.hdr");

        // Load HDR panorama (sphere) texture
        panorama = LoadTexture(skyboxFileName);

        // Generate cubemap (texture with 6 quads-cube-mapping) from panorama HDR texture
        // NOTE 1: New texture is generated rendering to texture, shader calculates the sphere->cube coordinates mapping
        // NOTE 2: It seems on some Android devices WebGL, fbo does not properly support a FLOAT-based attachment,
        // despite texture can be successfully created.. so using PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 instead of PIXELFORMAT_UNCOMPRESSED_R32G32B32A32
        skybox.materials[0].maps[MATERIAL_MAP_CUBEMAP].texture = GenTextureCubemap(shdrCubemap, panorama, 1024, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

        //UnloadTexture(panorama);    // Texture not required anymore, cubemap already generated
    }
    else
    {
        Image img = LoadImage("skybox.png");
        skybox.materials[0].maps[MATERIAL_MAP_CUBEMAP].texture = LoadTextureCubemap(img, CUBEMAP_LAYOUT_AUTO_DETECT); // CUBEMAP_LAYOUT_PANORAMA
        UnloadImage(img);
    }

    InitPhysics();
    SetPhysicsGravity(0, 0.1);
    InitPBR();
    InitAudioDevice();
    AddLight((Light){.pos = (Vector3){0, 5, 0}, .target = Vector3Zero(), .color = WHITE, .intensity = 1.0f, .type = SPOT, .on = 1});

    GuiEnable();

    Vector3 moveVelocity = Vector3Zero();

    Model grapplingGun = LoadModel("grapplingGun.glb");
    Ray grapple;
    Vector3 grappleHitPos;
    int grappleHitIndex;
    bool grappleAlreadyHit = false;
    bool isGrappling = true;
    grapplingGun.materials[0].maps[MATERIAL_MAP_ALBEDO].texture = LoadTexture("GrapplingAlbedo.png");
    SetTextureFilter(grapplingGun.materials[0].maps[MATERIAL_MAP_ALBEDO].texture, TEXTURE_FILTER_ANISOTROPIC_16X);
    grapplingGun.transform = MatrixTranslate(-1.0f, 0, 2.0f);
    bool grapplingUnlocked = false;
    bool grapplingEnabled = false;

    float unstableTimer = 0;
    int currentGroundIndex;
    int lastGroundIndex;
    float lastPlayerPos;

    Mesh platformHitBox = GenMeshCube(10, 150, 10);
    Model platform = LoadModelFromMesh(GenMeshCube(10, 1, 10));
    platform.materials[0] = LoadPBRMaterial("wood_color.png", 0, 0, "wood_normals.png", "wood_roughness.png", TEXTURE_FILTER_ANISOTROPIC_16X, false);
    Level lvl1;
    lvl1.elementAmount = 2;
    lvl1.groundArr = (Model*)malloc(lvl1.elementAmount*sizeof(Model));
    lvl1.groundArr[0] = platform;
    lvl1.groundArr[0].transform = MatrixMultiply(lvl1.groundArr[0].transform, MatrixTranslate(0, -2, 0));

    lvl1.groundArr[1] = platform;
    lvl1.groundArr[1].transform = MatrixMultiply(lvl1.groundArr[1].transform, MatrixTranslate(15, -2, 0));

    Level lvl2;
    lvl2.elementAmount = 3;
    lvl2.groundArr = (Model*)malloc(lvl2.elementAmount*sizeof(Model));
    lvl2.groundArr[0] = platform;
    lvl2.groundArr[0].transform = MatrixMultiply(lvl2.groundArr[0].transform, MatrixTranslate(0, -2, 0));

    lvl2.groundArr[1] = platform;
    lvl2.groundArr[1].transform = MatrixMultiply(lvl2.groundArr[1].transform, MatrixTranslate(15, -2, 0));

    lvl2.groundArr[2] = platform;
    lvl2.groundArr[2].transform = MatrixMultiply(lvl2.groundArr[2].transform, MatrixTranslate(30, -2, 0));

    Level lvl3;
    lvl3.elementAmount = 4;
    lvl3.groundArr = (Model*)malloc(lvl3.elementAmount*sizeof(Model));
    lvl3.groundArr[0] = platform;
    lvl3.groundArr[0].transform = MatrixMultiply(lvl3.groundArr[0].transform, MatrixTranslate(0, -2, 0));

    lvl3.groundArr[1] = platform;
    lvl3.groundArr[1].transform = MatrixMultiply(lvl3.groundArr[1].transform, MatrixTranslate(20, -2, 0));

    lvl3.groundArr[2] = platform;
    lvl3.groundArr[2].transform = MatrixMultiply(lvl3.groundArr[2].transform, MatrixTranslate(40, -2, 0));

    lvl3.groundArr[3] = platform;
    lvl3.groundArr[3].transform = MatrixMultiply(lvl3.groundArr[3].transform, MatrixTranslate(60, -2, 0));

    Level lvl4;
    lvl4.elementAmount = 6;
    lvl4.groundArr = (Model*)malloc(lvl4.elementAmount*sizeof(Model));
    lvl4.groundArr[0] = platform;
    lvl4.groundArr[0].transform = MatrixMultiply(lvl4.groundArr[0].transform, MatrixTranslate(0, -2, 0));

    lvl4.groundArr[1] = platform;
    lvl4.groundArr[1].transform = MatrixMultiply(lvl4.groundArr[1].transform, MatrixTranslate(15, 5, 0));

    lvl4.groundArr[2] = platform;
    lvl4.groundArr[2].transform = MatrixMultiply(lvl4.groundArr[2].transform, MatrixTranslate(30, 10, 0));

    lvl4.groundArr[3] = platform;
    lvl4.groundArr[3].transform = MatrixMultiply(lvl4.groundArr[3].transform, MatrixTranslate(45, -10, 0));

    lvl4.groundArr[4] = platform;
    lvl4.groundArr[4].transform = MatrixMultiply(lvl4.groundArr[4].transform, MatrixTranslate(60, -10, 0));

    lvl4.groundArr[5] = platform;
    lvl4.groundArr[5].transform = MatrixMultiply(lvl4.groundArr[5].transform, MatrixTranslate(75, -10, 0));

    Level lvl5;
    lvl5.elementAmount = 7;
    lvl5.groundArr = (Model*)malloc(lvl5.elementAmount*sizeof(Model));
    lvl5.groundArr[0] = platform;
    lvl5.groundArr[0].transform = MatrixMultiply(lvl5.groundArr[0].transform, MatrixTranslate(0, -2, 0));

    lvl5.groundArr[1] = platform;
    lvl5.groundArr[1].transform = MatrixMultiply(lvl5.groundArr[1].transform, MatrixTranslate(15, -10, 0));

    lvl5.groundArr[2] = platform;
    lvl5.groundArr[2].transform = MatrixMultiply(lvl5.groundArr[2].transform, MatrixTranslate(30, -20, 0));

    lvl5.groundArr[3] = platform;
    lvl5.groundArr[3].transform = MatrixMultiply(lvl5.groundArr[3].transform, MatrixTranslate(45, -30, 0));

    lvl5.groundArr[4] = platform;
    lvl5.groundArr[4].transform = MatrixMultiply(lvl5.groundArr[4].transform, MatrixTranslate(60, -40, 0));

    lvl5.groundArr[5] = platform;
    lvl5.groundArr[5].transform = MatrixMultiply(lvl5.groundArr[5].transform, MatrixTranslate(80, -30, 0));

    lvl5.groundArr[6] = platform;
    lvl5.groundArr[6].transform = MatrixMultiply(lvl5.groundArr[6].transform, MatrixTranslate(95, -30, 0));

    Level lvl6;
    lvl6.elementAmount = 7;
    lvl6.groundArr = (Model*)malloc(lvl6.elementAmount*sizeof(Model));
    lvl6.groundArr[0] = platform;
    lvl6.groundArr[0].transform = MatrixMultiply(lvl6.groundArr[0].transform, MatrixTranslate(0, -2, 0));

    lvl6.groundArr[1] = platform;
    lvl6.groundArr[1].transform = MatrixMultiply(lvl6.groundArr[1].transform, MatrixTranslate(15, -80, 0));

    lvl6.groundArr[2] = platform;
    lvl6.groundArr[2].transform = MatrixMultiply(lvl6.groundArr[2].transform, MatrixTranslate(30, -75, 0));

    lvl6.groundArr[3] = platform;
    lvl6.groundArr[3].transform = MatrixMultiply(lvl6.groundArr[3].transform, MatrixTranslate(45, -70, 0));

    lvl6.groundArr[4] = platform;
    lvl6.groundArr[4].transform = MatrixMultiply(lvl6.groundArr[4].transform, MatrixTranslate(60, -50, 0));

    lvl6.groundArr[5] = platform;
    lvl6.groundArr[5].transform = MatrixMultiply(lvl6.groundArr[5].transform, MatrixTranslate(80, -75, 0));

    lvl6.groundArr[6] = platform;
    lvl6.groundArr[6].transform = MatrixMultiply(lvl6.groundArr[6].transform, MatrixTranslate(95, -80, 0));

    Model* groundArr = lvl1.groundArr;


    Model nextLevel = LoadModelFromMesh(GenMeshCube(3, 3, 3));
    nextLevel.materials[0] = LoadPBRMaterial("gold_color.png", 0, 0, "gold_normals.png", "gold_roughness.png", TEXTURE_FILTER_ANISOTROPIC_16X, false);
    nextLevel.transform = MatrixMultiply(nextLevel.transform, MatrixTranslate(15, 3, 0));

    PhysicsBody groundPhysics = CreatePhysicsBodyRectangle((Vector2){0, 2}, 10, 1, 10);
    groundPhysics->enabled = false;
    groundPhysics->useGravity = false;
    groundPhysics->freezeOrient = true;
    PhysicsBody player = CreatePhysicsBodyRectangle(Vector2Zero(), 1, 1, 10);

    SetExitKey(KEY_NULL);

    Vector3 cubePosition = {0};

    float fallYVel;

    float timeSinceDeath;
    Vector3 targetAtDeath;

    Vector2 lastViewAngle;

    int groundArrSize = lvl1.elementAmount;

    int logoPositionX = screenWidth/2 - 128;
    int logoPositionY = screenHeight/2 - 128;

    int framesCounter = 0;
    int lettersCount = 0;

    int topSideRecWidth = 16;
    int leftSideRecHeight = 16;

    int bottomSideRecWidth = 16;
    int rightSideRecHeight = 16;

    int state = 0;                  // Tracking animation states (State Machine)
    float alpha = 1.0f;             // Useful for fading

    int framesSinceLaunch = 0;

    Sound jump = LoadSound("Jump.mp3");

    Music bgMusic = LoadMusicStream("background-music.mp3");

    Model instructions = LoadModelFromMesh(GenMeshCube(1, 20, 20));
    instructions.materials[0].maps[MATERIAL_MAP_ALBEDO].texture = LoadTexture("Instructions.png");
    instructions.transform = MatrixRotateXYZ((Vector3) {180*DEG2RAD, 0, 0});

    Texture2D instructions1 = LoadTexture("Instructions-1.png");

    SetTargetFPS(GetMonitorRefreshRate(GetCurrentMonitor())); // Set our game to run at 60 frames-per-second
    Font font = LoadFont("Debrosee-ALPnL.ttf");
    //--------------------------------------------------------------------------------------

    // Main game loop
    while (!WindowShouldClose()) // Detect window close button or ESC key
    {
        if (framesSinceLaunch < 10) framesSinceLaunch++;
        if (framesSinceLaunch == 1)
        {
            currentState = Playing;
        }
        else if (framesSinceLaunch == 2)
        {
            currentState = Respawn;
        }
        else if (framesSinceLaunch == 3)
        {
            currentState = Intro;
            cam.ViewCamera.target = (Vector3) {15, 0, 0};
        }
        int currentGround = -100;
        float dt = GetFrameTime();
        if (IsKeyPressed(KEY_F11))
        {
            ToggleFullscreen();
        }
        if (currentState == Playing)
        {
            PlayMusicStream(bgMusic);
            UpdateMusicStream(bgMusic);
            if (cam.CameraPosition.y < -90)
            {
                timeSinceDeath = 0.0f;
                targetAtDeath = cam.ViewCamera.target;
                currentState = Respawn;
                UpdateModelAnimation(playerModel, *playerAni, 7);
                fallYVel = 10;
                UseFPCameraMouse(&cam, false);
            }
            currentGroundIndex = -1;
            for (int i = 0; i < groundArrSize; i++)
            {
                RayHitInfo hit = GetCollisionRayMesh((Ray){Vector3Add(cam.CameraPosition, (Vector3){0, 100, 0}), (Vector3){0, -1, 0}}, groundArr[i].meshes[0], MatrixTranslate(groundArr[i].transform.m12, groundArr[i].transform.m13, groundArr[i].transform.m14));
                if (hit.hit)
                {
                    currentGround = hit.position.y;
                    currentGroundIndex = i;
                    break;
                }
            }
            if (IsKeyPressed(KEY_ESCAPE))
            {
                if (IsCursorHidden())
                {
                    UseFPCameraMouse(&cam, false);
                }
                else
                {
                    UseFPCameraMouse(&cam, true);
                }
            }
            if (IsKeyPressed(KEY_SPACE) && player->isGrounded)
            {
                PhysicsAddForce(player, (Vector2){0, -0.25});
                PlaySound(jump);
            }
            // Update
            //----------------------------------------------------------------------------------
            if (unstableTimer >= 3.0f)
            {
                groundArr[currentGroundIndex].transform = MatrixMultiply(groundArr[currentGroundIndex].transform, MatrixTranslate(sin(unstableTimer) / 100, 0, 0));
                groundArr[currentGroundIndex].transform = MatrixMultiply(groundArr[currentGroundIndex].transform, MatrixRotateX(sin(unstableTimer * 2) / 100));
                groundPhysics->enabled = true;
                groundPhysics->freezeOrient = false;
                groundPhysics->orient = groundPhysics->orient - (sin(unstableTimer * 2) / 100);
                float direction[MOVE_DOWN + 1] = {GetSpeedForAxis(&cam, MOVE_FRONT, cam.MoveSpeed.z),
                                                  GetSpeedForAxis(&cam, MOVE_BACK, cam.MoveSpeed.z),
                                                  GetSpeedForAxis(&cam, MOVE_RIGHT, cam.MoveSpeed.x),
                                                  GetSpeedForAxis(&cam, MOVE_LEFT, cam.MoveSpeed.x),
                                                  GetSpeedForAxis(&cam, MOVE_UP, cam.MoveSpeed.y),
                                                  GetSpeedForAxis(&cam, MOVE_DOWN, cam.MoveSpeed.y)};
                Vector3 Forward = Vector3Transform((Vector3){0, 0, 1}, MatrixRotateXYZ((Vector3){0, -cam.ViewAngles.x, 0}));

                Vector3 Right = (Vector3){Forward.z * -1.0f, 0, Forward.x};

                Vector3 move1 = Vector3Add(Vector3Zero(), Vector3Scale(Forward, direction[MOVE_FRONT] - direction[MOVE_BACK]));
                Vector3 move2 = Vector3Add(Vector3Zero(), Vector3Scale(Right, direction[MOVE_RIGHT] - direction[MOVE_LEFT]));
                player->velocity = Vector2Add((Vector2){(move1.z + move2.z)/75, 0}, player->velocity);
            }
            if (CheckCollisionBoxes((BoundingBox) {Vector3Add(cam.CameraPosition, (Vector3) {-2.5, -1, -2.5}), Vector3Add(cam.CameraPosition, (Vector3) {2.5, 1, 2.5})}, (BoundingBox) {Vector3Add((Vector3) {nextLevel.transform.m12, nextLevel.transform.m13, nextLevel.transform.m14}, (Vector3) {-2.5, -2.5, -2.5}), Vector3Add((Vector3) {nextLevel.transform.m12, nextLevel.transform.m13, nextLevel.transform.m14}, (Vector3) {2.5, 0.5, 2.5})}))
            {
                currentLevel++;
                cam.CameraPosition = Vector3Zero();
                player->velocity = Vector2Zero();
                player->force = Vector2Zero();
                player->position = Vector2Zero();
                unstableTimer = 0.0f;
                if (currentLevel == 1)
                {
                    groundArr = lvl2.groundArr;
                    nextLevel.transform.m12 = 30.0f;
                    groundArrSize = lvl2.elementAmount;
                }
                if (currentLevel == 2)
                {
                    groundArr = lvl3.groundArr;
                    nextLevel.transform.m12 = 60.0f;
                    groundArrSize = lvl3.elementAmount;
                    grapplingUnlocked = true;
                }
                if (currentLevel == 3)
                {
                    groundArr = lvl4.groundArr;
                    nextLevel.transform.m12 = 75.0f;
                    nextLevel.transform.m13 = -5.0f;
                    groundArrSize = lvl4.elementAmount;
                }
                if (currentLevel == 4)
                {
                    groundArr = lvl5.groundArr;
                    nextLevel.transform.m12 = 95.0f;
                    nextLevel.transform.m13 = -25.0f;
                    groundArrSize = lvl5.elementAmount;
                }
                if (currentLevel == 4)
                {
                    groundArr = lvl6.groundArr;
                    nextLevel.transform.m12 = 95.0f;
                    nextLevel.transform.m13 = -75.0f;
                    groundArrSize = lvl6.elementAmount;
                }
                if (currentLevel == 5)
                {
                    currentState = Finish;
                    UpdateModelAnimation(playerModel, *playerAni, 20);
                    cam.ViewCamera.position = Vector3Zero();
                    cam.CameraPosition = Vector3Zero();
                    cam.ViewCamera.target =  (Vector3) {15, 0, 0};
                    SetCameraMode(cam.ViewCamera, CAMERA_ORBITAL);
                    platform.transform = MatrixTranslate(15.0f, -2.0f, 0.0f);
                }
            }
            if (!player->isGrounded)
            {
                if (unstableTimer >= 3.0f)
                {
                    groundPhysics->freezeOrient = false;
                    groundPhysics->orient = 0.0f;
                    player->position.x = 0.0f;
                    player->velocity.x = 0.0f;
                    for (int i = 0; i < groundArrSize; i++)
                    {
                        groundArr[i].transform = MatrixTranslate(groundArr[i].transform.m12, groundArr[i].transform.m13, groundArr[i].transform.m14);
                    }
                }
                unstableTimer = 0;
            }
            else if (lastGroundIndex == currentGroundIndex)
            {
                unstableTimer += 1 * dt;
            }
            groundPhysics->position.y = -currentGround;
            UpdateFPCamera(&cam, unstableTimer >= 3.0f);
            UpdatePBR(cam.ViewCamera);
            grapplingGun.transform = MatrixMultiply(grapplingGun.transform,  MatrixRotateXYZ((Vector3){0, -(cam.ViewAngles.x - lastViewAngle.x), 0}));
            //grapplingGun.transform = MatrixRotateXYZ((Vector3){(cam.ViewAngles.y - lastViewAngle.y), 0, 0});
            if (isGrappling)
            {
                PhysicsAddForce(player, (Vector2) {0, -moveVelocity.y/100});
            }
            //printf("%f", groundPhysics->orient);
            UpdatePhysics();
            //printf("%f", groundPhysics->orient);
            groundPhysics->enabled = false;
            groundPhysics->freezeOrient = true;
            if (unstableTimer < 3.0f)
            {
                groundPhysics->freezeOrient = false;
                groundPhysics->orient = 0.0f;
                player->position.x = 0.0f;
                player->velocity.x = 0.0f;
            }
            cam.CameraPosition.y = -player->position.y;
            cam.CameraPosition.z += player->position.x - lastPlayerPos;
            // if (player->position.y > 10)
            // {
            //     ResetPhysics();
            //     cam.CameraPosition = Vector3Zero();
            //     for (int i = 0; i < groundArrSize; i++)
            //     {
            //         groundArr[i].transform = MatrixTranslate(groundArr[i].transform.m3, groundArr[i].transform.m7, groundArr[i].transform.m11);
            //     }
            //     unstableTimer = 0.0f;
            // }
            //----------------------------------------------------------------------------------

            // Draw
            //----------------------------------------------------------------------------------
            BeginDrawing();
            int bodiesCount = GetPhysicsBodiesCount();
            for (int i = 0; i < bodiesCount; i++)
            {
                PhysicsBody body = GetPhysicsBody(i);

                int vertexCount = GetPhysicsShapeVerticesCount(i);
                for (int j = 0; j < vertexCount; j++)
                {
                    // Get physics bodies shape vertices to draw lines
                    // Note: GetPhysicsShapeVertex() already calculates rotation transformations
                    Vector2 vertexA = GetPhysicsShapeVertex(body, j);

                    int jj = (((j + 1) < vertexCount) ? (j + 1) : 0); // Get next vertex or first to close the shape
                    Vector2 vertexB = GetPhysicsShapeVertex(body, jj);

                    //DrawLineV(Vector2Add(vertexA, (Vector2){screenWidth / 2, screenHeight / 2}), Vector2Add(vertexB, (Vector2){screenWidth / 2, screenHeight / 2}), GREEN); // Draw a line between two vertex positions
                }
            }

            ClearBackground(RAYWHITE);

            BeginModeFP3D(&cam);

            rlDisableDepthTest();
            rlDisableBackfaceCulling();
            rlDisableDepthMask();
            DrawModel(skybox, (Vector3){0, 0, 0}, 1.0f, WHITE);
            rlEnableBackfaceCulling();
            rlEnableDepthMask();
            rlEnableDepthTest();
            //DrawGrid(10, 1.0f);
            if (currentLevel == 0) DrawBillboard(cam.ViewCamera, instructions.materials[0].maps[MATERIAL_MAP_ALBEDO].texture, (Vector3) {5, 0, 0}, 10.0, WHITE);
            if (currentLevel == 2) DrawBillboard(cam.ViewCamera, instructions1, (Vector3) {5, 0, 0}, 10.0, WHITE);

            for (int i = 0; i < groundArrSize; i++)
            {
                DrawModel(groundArr[i], cubePosition, 1.0f, WHITE);
            }
            DrawModel(nextLevel, cubePosition, 1.0f, WHITE);
            if (grapplingUnlocked) DrawModel(grapplingGun, cam.CameraPosition, 1.0f, WHITE);
            if (grapplingUnlocked && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            {
                grapple.direction = cam.Forward;
                grappleAlreadyHit = false;
                for (int i = 0; i < groundArrSize; i++)
                {
                    if (i != currentGroundIndex && CheckCollisionRayBox((Ray) {Vector3Transform(Vector3Zero(), MatrixMultiply(grapplingGun.transform, MatrixTranslate(cam.CameraPosition.x, cam.CameraPosition.y, cam.CameraPosition.z))), cam.Forward}, (BoundingBox) {(Vector3) {groundArr[i].transform.m12 - 5, groundArr[i].transform.m13 - 50, groundArr[i].transform.m14 - 5}, (Vector3) {groundArr[i].transform.m12 + 5, groundArr[i].transform.m13 + 50, groundArr[i].transform.m14 + 5}}))
                    {
                        grapplingEnabled= true;
                        grappleHitIndex = i;
                        grappleAlreadyHit = true;
                        grappleHitPos = GetCollisionRayMesh((Ray) {Vector3Transform(Vector3Zero(), MatrixMultiply(grapplingGun.transform, MatrixTranslate(cam.CameraPosition.x, cam.CameraPosition.y, cam.CameraPosition.z))), cam.Forward}, platformHitBox, MatrixTranslate(groundArr[i].transform.m12, groundArr[i].transform.m13, groundArr[i].transform.m14)).position;
                        if (grappleHitPos.y > groundArr[i].transform.m13 + 0.5)
                        {
                            grappleHitPos.y = groundArr[i].transform.m13 + 0.5;
                        }
                        if (grappleHitPos.y < groundArr[i].transform.m13 - 0.5)
                        {
                            grappleHitPos.y = groundArr[i].transform.m13 - 0.5;
                        }
                        break;
                    }
                    else
                    {
                        if (!grappleAlreadyHit) grapplingEnabled = false;
                    }
                }
            }
            if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && grapplingUnlocked && grapplingEnabled)
            {
                isGrappling = true;
                Vector3 startPos = Vector3Transform(Vector3Zero(), MatrixMultiply(MatrixTranslate(cam.CameraPosition.x, cam.CameraPosition.y, cam.CameraPosition.z), MatrixTranslate(grapplingGun.transform.m12, grapplingGun.transform.m13, grapplingGun.transform.m14)));
                DrawLine3D(startPos, grappleHitPos, BLUE);
                grapple.position = Vector3Transform(Vector3Zero(), MatrixMultiply(grapplingGun.transform, MatrixTranslate(cam.CameraPosition.x, cam.CameraPosition.y, cam.CameraPosition.z)));
                moveVelocity = (Vector3) {atan(grappleHitPos.x-startPos.x), atan(grappleHitPos.y-startPos.y), atan(grappleHitPos.z-startPos.z)};
                if (moveVelocity.x == 0.0f && moveVelocity.y == 0.0f && moveVelocity.z == 0.0f) isGrappling = false;
                cam.CameraPosition = Vector3Add(cam.CameraPosition, (Vector3) {moveVelocity.x, 0, moveVelocity.z});
            }
            else
            {
                moveVelocity = Vector3Zero();
                grapplingEnabled= false;
                isGrappling = false;
            }
            //DrawModel(playerModel, cubePosition, 1.0f, WHITE);

            EndModeFP3D();

            EndDrawing();
            lastGroundIndex = currentGroundIndex;
            lastPlayerPos = player->position.x;
            lastViewAngle = cam.ViewAngles;
        }
        else if (currentState == Start)
        {
            UpdateMusicStream(bgMusic);
            int width = GetScreenWidth();
            int height = GetScreenHeight();
            UseFPCameraMouse(&cam, false);
            UpdateCamera(&cam.ViewCamera);
            BeginDrawing();
            ClearBackground(WHITE);
            //DrawTexturePro(background, (Rectangle){0, 0, background.width, background.height}, (Rectangle){0, 0, width, height}, Vector2Zero(), 0, WHITE);
            UpdatePBR(cam.ViewCamera);
            BeginMode3D(cam.ViewCamera);
            rlDisableDepthTest();
            rlDisableBackfaceCulling();
            rlDisableDepthMask();
            DrawModel(skybox, (Vector3){0, 0, 0}, 1.0f, WHITE);
            rlEnableBackfaceCulling();
            rlEnableDepthMask();
            rlEnableDepthTest();
            DrawModel(groundArr[1], cubePosition, 1.0f, WHITE);
            DrawModel(playerModel, (Vector3) {15, -5, -5}, 0.5f, WHITE);
            EndMode3D();
            if (GuiButton((Rectangle){width / 2 - width / 20, height / 2 - height / 20, width / 10, height / 10}, "PLAY"))
            {
                currentState = Playing;
                cam.CameraPosition = Vector3Zero();
                UseFPCameraMouse(&cam, true);
                player->velocity = Vector2Zero();
                player->force = Vector2Zero();
                player->position = Vector2Zero();
                unstableTimer = 0.0f;
            }
            DrawTextEx(font, "ROCKY ROAD", (Vector2){width/2-MeasureText("ROCKY ROAD", 20)*2, 100}, 100, 2.0f, RED);
            EndDrawing();
        }
        else if (currentState == Respawn)
        {
            UpdateMusicStream(bgMusic);
            timeSinceDeath += 0.01;
            int width = GetScreenWidth();
            int height = GetScreenHeight();
            fallYVel += 1;
            cam.CameraPosition = (Vector3){0, -90, 0};
            if (timeSinceDeath < 1.0f)
                cam.ViewCamera.target = Vector3Lerp(targetAtDeath, (Vector3){0, -90 - fallYVel, 0}, timeSinceDeath);
            else
                cam.ViewCamera.target = (Vector3) {0, -90 - fallYVel, 0};
            BeginDrawing();
            ClearBackground(WHITE);
            BeginMode3D(cam.ViewCamera);
            rlDisableDepthTest();
            rlDisableBackfaceCulling();
            rlDisableDepthMask();
            DrawModel(skybox, (Vector3){0, 0, 0}, 1.0f, WHITE);
            rlEnableBackfaceCulling();
            rlEnableDepthMask();
            rlEnableDepthTest();
            DrawModel(playerModel, (Vector3){0, -90 - fallYVel, 0}, 1.0f, WHITE);
            EndMode3D();
            if (GuiButton((Rectangle){width / 2 - width / 20 - 100, height / 2 - height / 20 - 100, width / 10, height / 10}, "RESPAWN"))
            {
                currentState = Playing;
                cam.CameraPosition = Vector3Zero();
                UseFPCameraMouse(&cam, true);
                player->velocity = Vector2Zero();
                player->force = Vector2Zero();
                player->position = Vector2Zero();
                unstableTimer = 0.0f;
            }
            EndDrawing();
        }
        else if (currentState == Intro)
        {
            UpdateMusicStream(bgMusic);
            if (state == 0)                 // State 0: Small box blinking
        {
            framesCounter++;

            if (framesCounter == 120)
            {
                state = 1;
                framesCounter = 0;      // Reset counter... will be used later...
            }
        }
        else if (state == 1)            // State 1: Top and left bars growing
        {
            topSideRecWidth += 4;
            leftSideRecHeight += 4;

            if (topSideRecWidth == 256) state = 2;
        }
        else if (state == 2)            // State 2: Bottom and right bars growing
        {
            bottomSideRecWidth += 4;
            rightSideRecHeight += 4;

            if (bottomSideRecWidth == 256) state = 3;
        }
        else if (state == 3)            // State 3: Letters appearing (one by one)
        {
            framesCounter++;

            if (framesCounter/12)       // Every 12 frames, one more letter!
            {
                lettersCount++;
                framesCounter = 0;
            }

            if (lettersCount >= 10)     // When all letters have appeared, just fade out everything
            {
                alpha -= 0.02f;

                if (alpha <= 0.0f)
                {
                    alpha = 0.0f;
                    state = 4;
                }
            }
        }
        else if (state == 4)            // State 4: Go to homescreen
        {
            currentState = Start;
        }
        //----------------------------------------------------------------------------------

        // Draw
        //----------------------------------------------------------------------------------
        BeginDrawing();

            ClearBackground(RAYWHITE);

            if (state == 0)
            {
                if ((framesCounter/15)%2) DrawRectangle(logoPositionX, logoPositionY, 16, 16, BLACK);
            }
            else if (state == 1)
            {
                DrawRectangle(logoPositionX, logoPositionY, topSideRecWidth, 16, BLACK);
                DrawRectangle(logoPositionX, logoPositionY, 16, leftSideRecHeight, BLACK);
            }
            else if (state == 2)
            {
                DrawRectangle(logoPositionX, logoPositionY, topSideRecWidth, 16, BLACK);
                DrawRectangle(logoPositionX, logoPositionY, 16, leftSideRecHeight, BLACK);

                DrawRectangle(logoPositionX + 240, logoPositionY, 16, rightSideRecHeight, BLACK);
                DrawRectangle(logoPositionX, logoPositionY + 240, bottomSideRecWidth, 16, BLACK);
            }
            else if (state == 3)
            {
                DrawRectangle(logoPositionX, logoPositionY, topSideRecWidth, 16, Fade(BLACK, alpha));
                DrawRectangle(logoPositionX, logoPositionY + 16, 16, leftSideRecHeight - 32, Fade(BLACK, alpha));

                DrawRectangle(logoPositionX + 240, logoPositionY + 16, 16, rightSideRecHeight - 32, Fade(BLACK, alpha));
                DrawRectangle(logoPositionX, logoPositionY + 240, bottomSideRecWidth, 16, Fade(BLACK, alpha));

                DrawRectangle(GetScreenWidth()/2 - 112, GetScreenHeight()/2 - 112, 224, 224, Fade(RAYWHITE, alpha));

                DrawText(TextSubtext("raylib", 0, lettersCount), GetScreenWidth()/2 - 44, GetScreenHeight()/2 + 48, 50, Fade(BLACK, alpha));
            }
            else if (state == 4)
            {
                DrawText("[R] REPLAY", 340, 200, 20, GRAY);
            }

        EndDrawing();
        }
        else if (currentState == Finish)
        {
            UpdateMusicStream(bgMusic);
            BeginDrawing();
            ClearBackground(WHITE);
            UpdateCamera(&cam.ViewCamera);
            UpdatePBR(cam.ViewCamera);
            BeginMode3D(cam.ViewCamera);
            rlDisableDepthTest();
            rlDisableBackfaceCulling();
            rlDisableDepthMask();
            DrawModel(skybox, (Vector3){0, 0, 0}, 1.0f, WHITE);
            rlEnableBackfaceCulling();
            rlEnableDepthMask();
            rlEnableDepthTest();
            DrawModel(platform, cubePosition, 1.0f, WHITE);
            DrawModel(playerModel, (Vector3) {15, 2, 0}, 0.5f, WHITE);
            EndMode3D();
            DrawTextEx(font, "VICTORY", (Vector2){GetScreenWidth()/2-MeasureText("VICTORY", 20)*2, 100}, 100, 2.0f, RED);
            EndDrawing();
        }

        //----------------------------------------------------------------------------------
    }

    // De-Initialization
    //--------------------------------------------------------------------------------------
    CloseWindow(); // Close window and OpenGL context
    UnloadPBRModel(platform);
    UnloadPBRModel(nextLevel);
    UnloadShader(skybox.materials[0].shader);
    UnloadShader(shdrCubemap);
    UnloadTexture(skybox.materials[0].maps[MATERIAL_MAP_CUBEMAP].texture);
    UnloadTexture(playerAlbedo);
    UnloadModel(playerModel);
    UnloadModel(grapplingGun);
    UnloadMesh(platformHitBox);
    UnloadTexture(instructions.materials[0].maps[MATERIAL_MAP_ALBEDO].texture);
    UnloadTexture(instructions1);
    UnloadModel(instructions);
    free(lvl1.groundArr);
    free(lvl2.groundArr);
    free(lvl3.groundArr);
    free(lvl4.groundArr);
    free(lvl5.groundArr);
    free(lvl6.groundArr);

    UnloadModel(skybox); // Unload skybox model

    UnloadMesh(cube);
    ClosePBR();
    ClosePhysics();
    CloseAudioDevice();
    //--------------------------------------------------------------------------------------

    return 0;
}

bool getCurrentGround(Vector3 pos, Model groundArr)
{
    if (pos.x > groundArr.meshes[0].vertices[0] && pos.x < groundArr.meshes[0].vertices[3] && pos.z > groundArr.meshes[0].vertices[14] && pos.z < groundArr.meshes[0].vertices[2])
    {
        return true;
    }
    return false;
}

void DrawTextCodepoint3D(Font font, int codepoint, Vector3 position, float fontSize, bool backface, Color tint)
{
    // Character index position in sprite font
    // NOTE: In case a codepoint is not available in the font, index returned points to '?'
    int index = GetGlyphIndex(font, codepoint);
    float scale = fontSize / (float)font.baseSize;

    // Character destination rectangle on screen
    // NOTE: We consider charsPadding on drawing
    position.x += (float)(font.chars[index].offsetX - font.charsPadding) / (float)font.baseSize * scale;
    position.z += (float)(font.chars[index].offsetY - font.charsPadding) / (float)font.baseSize * scale;

    // Character source rectangle from font texture atlas
    // NOTE: We consider chars padding when drawing, it could be required for outline/glow shader effects
    Rectangle srcRec = {font.recs[index].x - (float)font.charsPadding, font.recs[index].y - (float)font.charsPadding,
                        font.recs[index].width + 2.0f * font.charsPadding, font.recs[index].height + 2.0f * font.charsPadding};

    float width = (float)(font.recs[index].width + 2.0f * font.charsPadding) / (float)font.baseSize * scale;
    float height = (float)(font.recs[index].height + 2.0f * font.charsPadding) / (float)font.baseSize * scale;

    if (font.texture.id > 0)
    {
        const float x = 0.0f;
        const float y = 0.0f;
        const float z = 0.0f;

        // normalized texture coordinates of the glyph inside the font texture (0.0f -> 1.0f)
        const float tx = srcRec.x / font.texture.width;
        const float ty = srcRec.y / font.texture.height;
        const float tw = (srcRec.x + srcRec.width) / font.texture.width;
        const float th = (srcRec.y + srcRec.height) / font.texture.height;

        if (SHOW_LETTER_BOUNDRY)
            DrawCubeWiresV((Vector3){position.x + width / 2, position.y, position.z + height / 2}, (Vector3){width, LETTER_BOUNDRY_SIZE, height}, LETTER_BOUNDRY_COLOR);

        rlCheckRenderBatchLimit(4 + 4 * backface);
        rlSetTexture(font.texture.id);

        rlPushMatrix();
        rlTranslatef(position.x, position.y, position.z);

        rlBegin(RL_QUADS);
        rlColor4ub(tint.r, tint.g, tint.b, tint.a);

        // Front Face
        rlNormal3f(0.0f, 1.0f, 0.0f); // Normal Pointing Up
        rlTexCoord2f(tx, ty);
        rlVertex3f(x, y, z); // Top Left Of The Texture and Quad
        rlTexCoord2f(tx, th);
        rlVertex3f(x, y, z + height); // Bottom Left Of The Texture and Quad
        rlTexCoord2f(tw, th);
        rlVertex3f(x + width, y, z + height); // Bottom Right Of The Texture and Quad
        rlTexCoord2f(tw, ty);
        rlVertex3f(x + width, y, z); // Top Right Of The Texture and Quad

        if (backface)
        {
            // Back Face
            rlNormal3f(0.0f, -1.0f, 0.0f); // Normal Pointing Down
            rlTexCoord2f(tx, ty);
            rlVertex3f(x, y, z); // Top Right Of The Texture and Quad
            rlTexCoord2f(tw, ty);
            rlVertex3f(x + width, y, z); // Top Left Of The Texture and Quad
            rlTexCoord2f(tw, th);
            rlVertex3f(x + width, y, z + height); // Bottom Left Of The Texture and Quad
            rlTexCoord2f(tx, th);
            rlVertex3f(x, y, z + height); // Bottom Right Of The Texture and Quad
        }
        rlEnd();
        rlPopMatrix();

        rlSetTexture(0);
    }
}

void DrawText3D(Font font, const char *text, Vector3 position, float fontSize, float fontSpacing, float lineSpacing, bool backface, Color tint)
{
    int length = TextLength(text); // Total length in bytes of the text, scanned by codepoints in loop

    float textOffsetY = 0.0f; // Offset between lines (on line break '\n')
    float textOffsetX = 0.0f; // Offset X to next character to draw

    float scale = fontSize / (float)font.baseSize;

    for (int i = 0; i < length;)
    {
        // Get next codepoint from byte string and glyph index in font
        int codepointByteCount = 0;
        int *codepoint = GetCodepoints(&text[i], &codepointByteCount);
        int index = GetGlyphIndex(font, &codepoint);

        // NOTE: Normally we exit the decoding sequence as soon as a bad byte is found (and return 0x3f)
        // but we need to draw all of the bad bytes using the '?' symbol moving one byte
        if (codepoint == 0x3f)
            codepointByteCount = 1;

        if (codepoint == '\n')
        {
            // NOTE: Fixed line spacing of 1.5 line-height
            // TODO: Support custom line spacing defined by user
            textOffsetY += scale + lineSpacing / (float)font.baseSize * scale;
            textOffsetX = 0.0f;
        }
        else
        {
            if ((codepoint != ' ') && (codepoint != '\t'))
            {
                DrawTextCodepoint3D(font, codepoint, (Vector3){position.x + textOffsetX, position.y, position.z + textOffsetY}, fontSize, backface, tint);
            }

            if (font.chars[index].advanceX == 0)
                textOffsetX += (float)(font.recs[index].width + fontSpacing) / (float)font.baseSize * scale;
            else
                textOffsetX += (float)(font.chars[index].advanceX + fontSpacing) / (float)font.baseSize * scale;
        }

        i += codepointByteCount; // Move text bytes counter to next codepoint
    }
}

static TextureCubemap GenTextureCubemap(Shader shader, Texture2D panorama, int size, int format)
{
    TextureCubemap cubemap = {0};

    rlDisableBackfaceCulling(); // Disable backface culling to render inside the cube

    // STEP 1: Setup framebuffer
    //------------------------------------------------------------------------------------------
    unsigned int rbo = rlLoadTextureDepth(size, size, true);
    cubemap.id = rlLoadTextureCubemap(0, size, format);

    unsigned int fbo = rlLoadFramebuffer(size, size);
    rlFramebufferAttach(fbo, rbo, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);
    rlFramebufferAttach(fbo, cubemap.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_CUBEMAP_POSITIVE_X, 0);

    // Check if framebuffer is complete with attachments (valid)
    if (rlFramebufferComplete(fbo))
        TraceLog(LOG_INFO, "FBO: [ID %i] Framebuffer object created successfully", fbo);
    //------------------------------------------------------------------------------------------

    // STEP 2: Draw to framebuffer
    //------------------------------------------------------------------------------------------
    // NOTE: Shader is used to convert HDR equirectangular environment map to cubemap equivalent (6 faces)
    rlEnableShader(shader.id);

    // Define projection matrix and send it to shader
    Matrix matFboProjection = MatrixPerspective(90.0 * DEG2RAD, 1.0, RL_CULL_DISTANCE_NEAR, RL_CULL_DISTANCE_FAR);
    rlSetUniformMatrix(shader.locs[SHADER_LOC_MATRIX_PROJECTION], matFboProjection);

    // Define view matrix for every side of the cubemap
    Matrix fboViews[6] = {
        MatrixLookAt((Vector3){0.0f, 0.0f, 0.0f}, (Vector3){1.0f, 0.0f, 0.0f}, (Vector3){0.0f, -1.0f, 0.0f}),
        MatrixLookAt((Vector3){0.0f, 0.0f, 0.0f}, (Vector3){-1.0f, 0.0f, 0.0f}, (Vector3){0.0f, -1.0f, 0.0f}),
        MatrixLookAt((Vector3){0.0f, 0.0f, 0.0f}, (Vector3){0.0f, 1.0f, 0.0f}, (Vector3){0.0f, 0.0f, 1.0f}),
        MatrixLookAt((Vector3){0.0f, 0.0f, 0.0f}, (Vector3){0.0f, -1.0f, 0.0f}, (Vector3){0.0f, 0.0f, -1.0f}),
        MatrixLookAt((Vector3){0.0f, 0.0f, 0.0f}, (Vector3){0.0f, 0.0f, 1.0f}, (Vector3){0.0f, -1.0f, 0.0f}),
        MatrixLookAt((Vector3){0.0f, 0.0f, 0.0f}, (Vector3){0.0f, 0.0f, -1.0f}, (Vector3){0.0f, -1.0f, 0.0f})};

    rlViewport(0, 0, size, size); // Set viewport to current fbo dimensions

    // Activate and enable texture for drawing to cubemap faces
    rlActiveTextureSlot(0);
    rlEnableTexture(panorama.id);

    for (int i = 0; i < 6; i++)
    {
        // Set the view matrix for the current cube face
        rlSetUniformMatrix(shader.locs[SHADER_LOC_MATRIX_VIEW], fboViews[i]);

        // Select the current cubemap face attachment for the fbo
        // WARNING: This function by default enables->attach->disables fbo!!!
        rlFramebufferAttach(fbo, cubemap.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_CUBEMAP_POSITIVE_X + i, 0);
        rlEnableFramebuffer(fbo);

        // Load and draw a cube, it uses the current enabled texture
        rlClearScreenBuffers();
        rlLoadDrawCube();

        // ALTERNATIVE: Try to use internal batch system to draw the cube instead of rlLoadDrawCube
        // for some reason this method does not work, maybe due to cube triangles definition? normals pointing out?
        // TODO: Investigate this issue...
        //rlSetTexture(panorama.id); // WARNING: It must be called after enabling current framebuffer if using internal batch system!
        //rlClearScreenBuffers();
        //DrawCubeV(Vector3Zero(), Vector3One(), WHITE);
        //rlDrawRenderBatchActive();
    }
    //------------------------------------------------------------------------------------------

    // STEP 3: Unload framebuffer and reset state
    //------------------------------------------------------------------------------------------
    rlDisableShader();        // Unbind shader
    rlDisableTexture();       // Unbind texture
    rlDisableFramebuffer();   // Unbind framebuffer
    rlUnloadFramebuffer(fbo); // Unload framebuffer (and automatically attached depth texture/renderbuffer)

    // Reset viewport dimensions to default
    rlViewport(0, 0, rlGetFramebufferWidth(), rlGetFramebufferHeight());
    rlEnableBackfaceCulling();
    //------------------------------------------------------------------------------------------

    cubemap.width = size;
    cubemap.height = size;
    cubemap.mipmaps = 1;
    cubemap.format = format;

    return cubemap;
}

static float GetSpeedForAxis(FPCamera *camera, CameraControls axis, float speed)
{
    if (camera == NULL)
        return 0;

    int key = camera->ControlsKeys[axis];
    if (key == -1)
        return 0;

    float factor = 1.0f;
    if (IsKeyDown(camera->ControlsKeys[SPRINT]))
        factor = 2;

    if (IsKeyDown(camera->ControlsKeys[axis]))
        return speed * GetFrameTime() * factor;

    return 0.0f;
}