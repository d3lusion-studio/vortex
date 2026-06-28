#ifndef VORTEX_GAME_API_H
#define VORTEX_GAME_API_H

#ifdef __cplusplus
extern "C" {
#endif

#define VORTEX_GAME_API_VERSION 1

typedef struct GameBox {
    float x, y;        /* centre, world space */
    float w, h;        /* size */
    float r, g, b, a;  /* colour */
} GameBox;

typedef struct GameContext {
    void*              memory;
    unsigned long long memorySize;

    float dt;
    float time;
    int   width, height;

    int inLeft, inRight, inUp, inDown;

    GameBox* boxes;
    int      boxCap;
    int      boxCount;
} GameContext;

typedef struct GameApi {
    int version;
    void (*on_load)(GameContext*);
    void (*on_unload)(GameContext*);
    int  (*update)(GameContext*);
} GameApi;

const GameApi* vortex_game_api(void);

#ifdef __cplusplus
}
#endif

#endif
