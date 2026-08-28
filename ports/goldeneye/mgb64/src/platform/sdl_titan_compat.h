#ifndef MGB64_SDL_TITAN_COMPAT_H
#define MGB64_SDL_TITAN_COMPAT_H

/* Compile-time compatibility surface only.  Titan input/audio never flow
 * through these functions; platform_titan.c and the TGR PCM lane own them.
 * Keeping the small type surface lets the upstream compatibility unit remain
 * source-compatible without linking SDL or pthread on R5900. */
#include <stdint.h>

typedef uint8_t Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef uint32_t SDL_AudioDeviceID;
typedef int SDL_Scancode;
typedef int SDL_GameControllerButton;
typedef struct SDL_GameController { int unused; } SDL_GameController;
typedef struct SDL_Window { int unused; } SDL_Window;
typedef struct SDL_mutex { int unused; } SDL_mutex;
typedef struct SDL_sem { int unused; } SDL_sem;

typedef struct SDL_AudioSpec
{
    int freq;
    Uint16 format;
    Uint8 channels;
    Uint8 silence;
    Uint16 samples;
    Uint16 padding;
    Uint32 size;
    void (*callback)(void *, Uint8 *, int);
    void *userdata;
} SDL_AudioSpec;

#define SDL_INIT_AUDIO 0x10u
#define AUDIO_S16SYS 0x8010u
#define SDL_AUDIODEVICEREMOVED 0x1102u

enum {
    SDL_CONTROLLER_BUTTON_A=0, SDL_CONTROLLER_BUTTON_B,
    SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
    SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_GUIDE,
    SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_LEFTSTICK,
    SDL_CONTROLLER_BUTTON_RIGHTSTICK, SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
    SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_DPAD_UP,
    SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
    SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_MAX
};
enum {
    SDL_CONTROLLER_AXIS_LEFTX=0, SDL_CONTROLLER_AXIS_LEFTY,
    SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY,
    SDL_CONTROLLER_AXIS_TRIGGERLEFT, SDL_CONTROLLER_AXIS_TRIGGERRIGHT
};
enum {
    SDL_SCANCODE_A=4, SDL_SCANCODE_D=7, SDL_SCANCODE_F=9,
    SDL_SCANCODE_I=12, SDL_SCANCODE_J=13, SDL_SCANCODE_K=14,
    SDL_SCANCODE_L=15, SDL_SCANCODE_S=22, SDL_SCANCODE_W=26,
    SDL_SCANCODE_RETURN=40, SDL_SCANCODE_ESCAPE=41, SDL_SCANCODE_BACKSPACE=42,
    SDL_SCANCODE_TAB=43, SDL_SCANCODE_SPACE=44,
    SDL_SCANCODE_RIGHT=79, SDL_SCANCODE_LEFT=80,
    SDL_SCANCODE_DOWN=81, SDL_SCANCODE_UP=82, SDL_SCANCODE_LALT=226
};

#define SDL_BUTTON_LEFT 1u
#define SDL_BUTTON_MIDDLE 2u
#define SDL_BUTTON_RIGHT 3u
#define SDL_BUTTON(X) (1u << ((X)-1u))

static inline int SDL_InitSubSystem(Uint32 flags) { (void)flags; return 0; }
static inline const char *SDL_GetError(void) { return "Titan native backend"; }
static inline SDL_mutex *SDL_CreateMutex(void) { return (SDL_mutex *)(uintptr_t)1u; }
static inline void SDL_DestroyMutex(SDL_mutex *mutex) { (void)mutex; }
static inline int SDL_LockMutex(SDL_mutex *mutex) { (void)mutex; return 0; }
static inline int SDL_UnlockMutex(SDL_mutex *mutex) { (void)mutex; return 0; }
static inline SDL_AudioDeviceID SDL_OpenAudioDevice(
    const char *device, int capture, const SDL_AudioSpec *want,
    SDL_AudioSpec *have, int changes)
{
    (void)device; (void)capture; (void)changes;
    if (want == 0) return 0u;
    if (have != 0) *have = *want;
    return 1u;
}
static inline void SDL_CloseAudioDevice(SDL_AudioDeviceID device) { (void)device; }
static inline void SDL_PauseAudioDevice(SDL_AudioDeviceID device, int pause)
{ (void)device; (void)pause; }
static inline Uint32 SDL_GetQueuedAudioSize(SDL_AudioDeviceID device)
{ (void)device; return 0u; }
static inline int SDL_QueueAudio(SDL_AudioDeviceID device, const void *data, Uint32 bytes)
{ (void)device; (void)data; (void)bytes; return -1; }
static inline const Uint8 *SDL_GetKeyboardState(int *count)
{
    static const Uint8 keys[256] = {0};
    if (count != 0) *count = 256;
    return keys;
}
static inline Uint32 SDL_GetMouseState(int *x, int *y)
{ if (x) *x=0; if (y) *y=0; return 0u; }
static inline int SDL_GameControllerGetAxis(SDL_GameController *pad, int axis)
{ (void)pad; (void)axis; return 0; }
static inline void SDL_GL_SwapWindow(SDL_Window *window) { (void)window; }
static inline void SDL_GL_GetDrawableSize(SDL_Window *window, int *width, int *height)
{ (void)window; if (width) *width=640; if (height) *height=448; }
static inline int SDL_atoi(const char *text)
{
    int sign=1,value=0;
    if (text==0) return 0;
    while (*text==' '||*text=='\t'||*text=='\r'||*text=='\n') text++;
    if (*text=='-') { sign=-1; text++; } else if (*text=='+') text++;
    while (*text>='0'&&*text<='9') { value=value*10+(*text-'0'); text++; }
    return value*sign;
}
static inline Uint8 SDL_GameControllerGetButton(
    SDL_GameController *pad, SDL_GameControllerButton button)
{ (void)pad; (void)button; return 0u; }

#endif
