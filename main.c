#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720
#define TOOLBAR_HEIGHT 70
#define TOOLBAR_MARGIN 8

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *canvas_texture = NULL;
static SDL_Texture *canvas_texture_preview = NULL;
static SDL_FRect canvas_rect = {0, TOOLBAR_HEIGHT, WINDOW_WIDTH,
                                WINDOW_HEIGHT - TOOLBAR_HEIGHT};
static SDL_Texture *toolbar_texture = NULL;
static SDL_FRect toolbar_rect = {0, 0, WINDOW_WIDTH, TOOLBAR_HEIGHT};
static SDL_FRect current_color_rect = {-1, -1, -1, -1};

typedef enum tool { NONE, BRUSH, ERASER, LINE, BOX, FILL } tool;
typedef enum button_type { TOOL, PALETTE, SIZE } button_type;

const char *palette_colors[] = {
    "#000000", "#ffffff", "#7f7f7f", "#c3c3c3", "#880015", "#b97a57", "#ed1c24",
    "#ffaec9", "#ff7f27", "#ffc90e", "#fff200", "#efe4b0", "#22b14c", "#b5e61d",
    "#00a2e8", "#99d9ea", "#3f48cc", "#7092be", "#a349a4", "#c8bfe7"};

typedef struct Button {
    SDL_FRect rect;
    const char *text;
    bool selected;
    bool hovered;
    button_type type;
    tool tool;
    SDL_Color color;
    float brush_size;
} Button;

typedef struct ButtonNode {
    Button button;
    struct ButtonNode *next;
} ButtonNode;

struct GlobalState {
    float xprev;
    float yprev;
    bool mouse_on_canvas;
    ButtonNode *buttons;
    tool tool;
    SDL_Color color;
    float brush_size;
    float xstart;
    float ystart;
    bool drag_in_progress;
    bool prev_motion_on_canvas;
};

struct GlobalState state = {.xprev = -1.0f,
                            .yprev = -1.0f,
                            .tool = BRUSH,
                            .color = {0, 0, 0},
                            .brush_size = 2};

SDL_Color color_from_string(const char *str) {
    SDL_Color c;
    sscanf(str, "#%02hhx%02hhx%02hhx", &c.r, &c.g, &c.b);
    c.a = SDL_ALPHA_OPAQUE;
    return c;
}

void render_button(SDL_Renderer *renderer, Button *button) {
    if (button->selected) {
        SDL_SetRenderDrawColor(renderer, button->color.r - 20,
                               button->color.g - 20, button->color.b,
                               SDL_ALPHA_OPAQUE);
        SDL_RenderFillRect(renderer, &button->rect);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
        SDL_RenderRect(renderer, &button->rect);
        SDL_RenderDebugText(renderer, button->rect.x + 4, button->rect.y + 4,
                            button->text);
    } else {
        SDL_SetRenderDrawColor(renderer, button->color.r, button->color.g,
                               button->color.b, SDL_ALPHA_OPAQUE);
        SDL_RenderFillRect(renderer, &button->rect);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
        SDL_RenderDebugText(renderer, button->rect.x + 4, button->rect.y + 4,
                            button->text);
    }

    if (button->brush_size > 0) {
        struct SDL_FRect size_rect = {
            button->rect.x + button->rect.w / 8,
            button->rect.y + button->rect.h / 2 - button->brush_size,
            button->rect.w * 0.75, button->brush_size * 2};
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
        SDL_RenderFillRect(renderer, &size_rect);
    }
}

Button new_button(SDL_Renderer *renderer, float x, float y, float w, float h,
                  button_type type, bool selected, const char *text, SDL_Color color,
                  tool tool, float brush_size) {
    struct SDL_FRect rect = {x, y, w, h};
    struct Button button = {rect, text, selected, false,
                            type, tool, color,    brush_size};

    render_button(renderer, &button);

    struct ButtonNode *node = (ButtonNode *)malloc(sizeof(ButtonNode));
    node->button = button;
    node->next = state.buttons;
    state.buttons = node;
    return button;
}

float toolbar_button_offset = TOOLBAR_MARGIN;
void new_tool_button(SDL_Renderer *renderer, tool tool, const char *text) {
    bool selected = tool == state.tool ? true : false;
    new_button(renderer, toolbar_button_offset, TOOLBAR_MARGIN,
               TOOLBAR_HEIGHT - (TOOLBAR_MARGIN * 2),
               TOOLBAR_HEIGHT - (TOOLBAR_MARGIN * 2), TOOL, selected, text,
               (SDL_Color){150, 150, 150, SDL_ALPHA_OPAQUE}, tool, -1);
    toolbar_button_offset += TOOLBAR_HEIGHT - TOOLBAR_MARGIN;
}
void new_palette_button(SDL_Renderer *renderer, SDL_Color color, bool lower_row) {
    if (!lower_row) {
        new_button(renderer, toolbar_button_offset, (TOOLBAR_HEIGHT / 4),
                   (TOOLBAR_HEIGHT / 4), (TOOLBAR_HEIGHT / 4), PALETTE, false,
                   "", color, NONE, -1);
    } else {
        new_button(renderer, toolbar_button_offset, (TOOLBAR_HEIGHT / 2),
                   (TOOLBAR_HEIGHT / 4), (TOOLBAR_HEIGHT / 4), PALETTE, false,
                   "", color, NONE, -1);
        toolbar_button_offset += TOOLBAR_HEIGHT / 4;
    }
}

void new_brush_size_button(SDL_Renderer *renderer, float brush_size) {
    bool selected = brush_size == state.brush_size ? true : false;
    new_button(renderer, toolbar_button_offset, TOOLBAR_MARGIN,
               TOOLBAR_HEIGHT - (TOOLBAR_MARGIN * 2),
               TOOLBAR_HEIGHT - (TOOLBAR_MARGIN * 2), SIZE, selected, "",
               (SDL_Color){150, 150, 150, SDL_ALPHA_OPAQUE}, NONE, brush_size);
    toolbar_button_offset += TOOLBAR_HEIGHT - TOOLBAR_MARGIN;
}

void free_buttons() {
    ButtonNode *head = state.buttons;
    while (head != NULL) {
        struct ButtonNode *tmp = head;
        head = tmp->next;
        free(tmp);
    }
}

SDL_FPoint *new_point(float x, float y) { return &(SDL_FPoint){x, y}; }

static inline void clear_canvas_preview() {
    SDL_SetRenderTarget(renderer, canvas_texture_preview);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_TRANSPARENT);
    SDL_RenderClear(renderer);
}

void render_filled_circle(SDL_Renderer *renderer, int x, int y, int radius) {
    int offsetx = 0;
    int offsety = radius;
    int d = radius - 1;

    while (offsety >= offsetx) {

        SDL_RenderLine(renderer, x - offsety, y + offsetx, x + offsety,
                       y + offsetx);
        SDL_RenderLine(renderer, x - offsetx, y + offsety, x + offsetx,
                       y + offsety);
        SDL_RenderLine(renderer, x - offsetx, y - offsety, x + offsetx,
                       y - offsety);
        SDL_RenderLine(renderer, x - offsety, y - offsetx, x + offsety,
                       y - offsetx);

        if (d >= 2 * offsetx) {
            d -= 2 * offsetx + 1;
            offsetx += 1;
        } else if (d < 2 * (radius - offsety)) {
            d += 2 * offsety - 1;
            offsety -= 1;
        } else {
            d += 2 * (offsety - offsetx - 1);
            offsety -= 1;
            offsetx += 1;
        }
    }
}

void tool_line(SDL_Renderer *renderer, SDL_Texture *texture, float x0, float y0,
               float x1, float y1, bool circle_shape) {
    SDL_SetRenderTarget(renderer, texture);
    if (state.tool == ERASER)
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
    else
        SDL_SetRenderDrawColor(renderer, state.color.r, state.color.g,
                               state.color.b, SDL_ALPHA_OPAQUE);

    int dx = abs(x1 - x0);
    int stepx = x0 < x1 ? 1 : -1;
    int dy = abs(y1 - y0);
    int stepy = y0 < y1 ? 1 : -1;
    int err = (dx > dy ? dx : -dy) / 2, e2;

    while (true) {
        if (circle_shape) {
            render_filled_circle(renderer, x0, y0, state.brush_size);
        } else {
            SDL_FRect rect = {x0, y0, state.brush_size * 2,
                              state.brush_size * 2};
            SDL_RenderFillRect(renderer, &rect);
        }
        if (x0 == x1 && y0 == y1)
            break;
        e2 = err;
        if (e2 > -dx) {
            err -= dy;
            x0 += stepx;
        }
        if (e2 < dy) {
            err += dx;
            y0 += stepy;
        }
    }
}

void tool_box(SDL_Renderer *renderer, SDL_Texture *texture, float x0, float y0,
              float x1, float y1) {

    if (x0 == x1 && y0 == y1)
        return;
    SDL_SetRenderTarget(renderer, texture);
    SDL_SetRenderDrawColor(renderer, state.color.r, state.color.g,
                           state.color.b, SDL_ALPHA_OPAQUE);

    SDL_FRect rect;

    if (abs(x1 - x0) < state.brush_size * 4 ||
        abs(y1 - y0) < state.brush_size * 4) {
        rect = (SDL_FRect){x0, y0, x1 - x0, y1 - y0};
        SDL_RenderFillRect(renderer, &rect);
        return;
    }

    if (x1 > x0) {
        if (y1 > y0) {
            rect = (SDL_FRect){x0, y0, x1 - x0, state.brush_size * 2};
            SDL_RenderFillRect(renderer, &rect);
            rect = (SDL_FRect){x0, y1 - state.brush_size * 2, x1 - x0,
                               state.brush_size * 2};
            SDL_RenderFillRect(renderer, &rect);
        } else {
            rect = (SDL_FRect){x0, y0 - state.brush_size * 2, x1 - x0,
                               state.brush_size * 2};
            SDL_RenderFillRect(renderer, &rect);
            rect = (SDL_FRect){x0, y1, x1 - x0, state.brush_size * 2};
            SDL_RenderFillRect(renderer, &rect);
        }
        rect = (SDL_FRect){x0, y0, state.brush_size * 2, y1 - y0};
        SDL_RenderFillRect(renderer, &rect);
        rect = (SDL_FRect){x1 - state.brush_size * 2, y0, state.brush_size * 2,
                           y1 - y0};
        SDL_RenderFillRect(renderer, &rect);
    } else {
        if (y1 > y0) {
            rect = (SDL_FRect){x0, y0, x1 - x0, state.brush_size * 2};
            SDL_RenderFillRect(renderer, &rect);
            rect = (SDL_FRect){x0, y1 - state.brush_size * 2, x1 - x0,
                               state.brush_size * 2};
            SDL_RenderFillRect(renderer, &rect);
        } else {
            rect = (SDL_FRect){x0, y0 - state.brush_size * 2, x1 - x0,
                               state.brush_size * 2};
            SDL_RenderFillRect(renderer, &rect);
            rect = (SDL_FRect){x0, y1, x1 - x0, state.brush_size * 2};
            SDL_RenderFillRect(renderer, &rect);
        }

        rect = (SDL_FRect){x0 - state.brush_size * 2, y0, state.brush_size * 2,
                           y1 - y0};
        SDL_RenderFillRect(renderer, &rect);
        rect = (SDL_FRect){x1, y0, state.brush_size * 2, y1 - y0};
        SDL_RenderFillRect(renderer, &rect);
    }
}

void tool_brush(SDL_Renderer *renderer, float x, float y, float xrel,
                float yrel) {
    SDL_SetRenderTarget(renderer, canvas_texture);
    if (state.tool == ERASER)
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
    else
        SDL_SetRenderDrawColor(renderer, state.color.r, state.color.g,
                               state.color.b, SDL_ALPHA_OPAQUE);

    float dist_sqared = (xrel * xrel) + (yrel * yrel);
    if (dist_sqared >= state.brush_size * state.brush_size) {
        SDL_SetRenderDrawColor(renderer, state.color.r, state.color.g,
                               state.color.b, SDL_ALPHA_OPAQUE);
        tool_line(renderer, canvas_texture, state.xprev, state.yprev, x, y,
                  true);
    }

    render_filled_circle(renderer, x, y, state.brush_size);
};

void tool_fill(SDL_Renderer *renderer, float x, float y) {
    SDL_SetRenderTarget(renderer, canvas_texture);
    SDL_Rect rect = {canvas_rect.x, canvas_rect.y, canvas_rect.w,
                     canvas_rect.h};
    SDL_Surface *surface = SDL_RenderReadPixels(renderer, NULL);
    if (surface == NULL)
        return;

    Uint8 r, g, b, a;
    int i, j;

    if (!SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a)) {
        SDL_Log(SDL_GetError());
        return;
    }

    Uint32 *pixels = surface->pixels;
    Uint32 source_color = b | (g << 8) | (r << 16) | (a << 24);

    int *queue = (int *)malloc(sizeof(int) * (canvas_rect.w * canvas_rect.h));
    memset(queue, -1, canvas_rect.w * canvas_rect.h * sizeof(int));
    queue[0] = (canvas_rect.w * y) + x;

    int index = 0;
    int counter = 1;
    while (1) {
        if (queue[index] == -1)
            break;

        i = queue[index] % rect.w;       // x coord
        j = (queue[index] - 1) / rect.w; // y coord

        SDL_WriteSurfacePixel(surface, i, j, state.color.r, state.color.g,
                              state.color.b, 255);
        if (i - 1 >= 0 && pixels[(j * rect.w) + (i - 1)] == source_color) {
            queue[counter] = (rect.w * j) + (i - 1); // queue x-1
            SDL_WriteSurfacePixel(surface, i - 1, j, state.color.r,
                                  state.color.g, state.color.b, 255);
            counter++;
        }
        if (i + 1 < rect.w && pixels[(j * rect.w) + (i + 1)] == source_color) {
            queue[counter] = (rect.w * j) + (i + 1); // queue x+1
            SDL_WriteSurfacePixel(surface, i + 1, j, state.color.r,
                                  state.color.g, state.color.b, 255);
            counter++;
        }
        if (j - 1 > rect.y && pixels[((j - 1) * rect.w) + i] == source_color) {
            queue[counter] = (rect.w * (j - 1)) + (i); // queue y+1
            SDL_WriteSurfacePixel(surface, i, j - 1, state.color.r,
                                  state.color.g, state.color.b, 255);
            counter++;
        }
        if (j + 1 < rect.h + rect.y &&
            pixels[((j + 1) * rect.w) + i] == source_color) {
            queue[counter] = (rect.w * (j + 1)) + (i); // queue y-1
            SDL_WriteSurfacePixel(surface, i, j + 1, state.color.r,
                                  state.color.g, state.color.b, 255);
            counter++;
        }
        index++;
    }
    free(queue);
    SDL_UpdateTexture(canvas_texture, NULL, surface->pixels, surface->pitch);
    SDL_DestroySurface(surface);
}

void canvas_handle_click(SDL_Event *event) {
    switch (state.tool) {
    case BRUSH:
    case ERASER:
        tool_brush(renderer, event->button.x, event->button.y,
                   event->motion.xrel, event->motion.yrel);
        break;
    case LINE:
        state.xstart = event->button.x;
        state.ystart = event->button.y;
        SDL_Log("line start %f, %f", event->button.x, event->button.y);
        state.drag_in_progress = true;
        break;
    case BOX:
        state.xstart = event->button.x;
        state.ystart = event->button.y;
        SDL_Log("box start %f, %f", event->button.x, event->button.y);
        state.drag_in_progress = true;
        break;
    case FILL:
        tool_fill(renderer, event->button.x, event->button.y);
    default:
        break;
    }
}

void toolbar_handle_click(SDL_Event *event) {
    ButtonNode *curr = state.buttons;
    while (curr != NULL) {
        if (SDL_PointInRectFloat(new_point(event->button.x, event->button.y),
                                 &curr->button.rect)) {
            switch (curr->button.type) {
            case TOOL:
                if (state.tool != curr->button.tool) {
                    SDL_SetRenderTarget(renderer, toolbar_texture);
                    ButtonNode *curr1 = state.buttons;
                    while (curr1 != NULL) {
                        if (curr1->button.tool == state.tool) {
                            curr1->button.selected = false;
                            render_button(renderer, &curr1->button);
                            break;
                        }
                        curr1 = curr1->next;
                    }
                    state.tool = curr->button.tool;
                    curr->button.selected = true;
                    render_button(renderer, &curr->button);
                }
                break;
            case SIZE:
                if (state.brush_size != curr->button.brush_size) {
                    SDL_SetRenderTarget(renderer, toolbar_texture);
                    ButtonNode *curr1 = state.buttons;
                    while (curr1 != NULL) {
                        if (curr1->button.brush_size == state.brush_size) {
                            curr1->button.selected = false;
                            render_button(renderer, &curr1->button);
                            break;
                        }
                        curr1 = curr1->next;
                    }
                    state.brush_size = curr->button.brush_size;
                    curr->button.selected = true;
                    render_button(renderer, &curr->button);
                }
                break;
            case PALETTE:
                state.color = curr->button.color;
                SDL_SetRenderTarget(renderer, toolbar_texture);
                SDL_SetRenderDrawColor(renderer, state.color.r, state.color.g,
                                       state.color.b, SDL_ALPHA_OPAQUE);
                SDL_RenderFillRect(renderer, &current_color_rect);
                break;

            default:
                break;
            }
            break;
        }
        curr = curr->next;
    }
}

// input events
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    switch (event->type) {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;
        break;
    case SDL_EVENT_KEY_DOWN:
        switch (event->key.scancode) {
        /* Quit. */
        case SDL_SCANCODE_ESCAPE:
        case SDL_SCANCODE_Q:
            return SDL_APP_SUCCESS;
        default:
            break;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event->button.button == SDL_BUTTON_LEFT) {
            state.mouse_on_canvas = event->button.y > TOOLBAR_HEIGHT;
            if (state.mouse_on_canvas) {
                canvas_handle_click(event);
            } else {
                toolbar_handle_click(event);
            }
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event->button.button == SDL_BUTTON_LEFT) {
            switch (state.tool) {
            case LINE:
                if (state.drag_in_progress) {
                    clear_canvas_preview();
                    tool_line(renderer, canvas_texture, state.xstart,
                              state.ystart, event->button.x, event->button.y,
                              false);
                    SDL_Log("line end %f, %f", event->button.x,
                            event->button.y);
                }
                break;
            case BOX:
                if (state.drag_in_progress) {
                    clear_canvas_preview();
                    tool_box(renderer, canvas_texture, state.xstart,
                             state.ystart, event->button.x, event->button.y);
                    SDL_Log("box end %f, %f", event->button.x, event->button.y);
                }
                break;
            default:
                break;
            }
            state.drag_in_progress = false;
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        state.mouse_on_canvas = event->motion.y > TOOLBAR_HEIGHT;
        if (event->motion.state == SDL_BUTTON_LMASK) {
            if (state.mouse_on_canvas &&
                (state.tool == BRUSH || state.tool == ERASER)) {
                tool_brush(renderer, event->motion.x, event->motion.y,
                           event->motion.xrel, event->motion.yrel);
            } else if (state.drag_in_progress) {
                // temp line progress
                clear_canvas_preview();

                if (state.tool == LINE)
                    tool_line(renderer, canvas_texture_preview, state.xstart,
                              state.ystart, event->button.x, event->button.y,
                              false);
                else if (state.tool == BOX)
                    tool_box(renderer, canvas_texture_preview, state.xstart,
                             state.ystart, event->button.x, event->button.y);
            }
        } else {
            state.drag_in_progress = false;
        }
        state.xprev = event->motion.x;
        state.yprev = event->motion.y;
        break;
    default:
        break;
    }
    return SDL_APP_CONTINUE;
}

// once per frame
SDL_AppResult SDL_AppIterate(void *appstate) {
    char debug_text[1024] = {};

    SDL_SetRenderTarget(renderer, NULL);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, canvas_texture, &canvas_rect, &canvas_rect);
    SDL_RenderTexture(renderer, canvas_texture_preview, &canvas_rect,
                      &canvas_rect);
    SDL_RenderTexture(renderer, toolbar_texture, &toolbar_rect, &toolbar_rect);
    SDL_RenderDebugText(renderer, 0, 8, debug_text);

    SDL_RenderPresent(renderer);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
    SDL_SetAppMetadata("Paint", "0.1", "com.shezdy.paint");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_CreateWindowAndRenderer("examples/renderer/clear", WINDOW_WIDTH,
                                     WINDOW_HEIGHT, 0, &window, &renderer)) {
        SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    int w, h;
    SDL_GetRenderOutputSize(renderer, &w, &h);
    canvas_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                       SDL_TEXTUREACCESS_TARGET, w, h);
    if (!canvas_texture) {
        SDL_Log("Couldn't create texture: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderTarget(renderer, canvas_texture);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);

    canvas_texture_preview = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (!canvas_texture_preview) {
        SDL_Log("Couldn't create texture: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderTarget(renderer, canvas_texture_preview);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_TRANSPARENT);
    SDL_RenderClear(renderer);

    toolbar_texture =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                          SDL_TEXTUREACCESS_TARGET, w, TOOLBAR_HEIGHT);
    if (!toolbar_texture) {
        SDL_Log("Couldn't create texture: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderTarget(renderer, toolbar_texture);
    SDL_SetRenderDrawColor(renderer, 200, 200, 200, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);

    new_tool_button(renderer, BRUSH, "brush");
    new_tool_button(renderer, ERASER, "erase");
    new_tool_button(renderer, LINE, "line");
    new_tool_button(renderer, BOX, "box");
    new_tool_button(renderer, FILL, "fill");

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderLine(renderer, toolbar_button_offset, 0, toolbar_button_offset,
                   TOOLBAR_HEIGHT);
    toolbar_button_offset += TOOLBAR_MARGIN;

    new_brush_size_button(renderer, 0.5);
    new_brush_size_button(renderer, 1);
    new_brush_size_button(renderer, 2);
    new_brush_size_button(renderer, 4);
    new_brush_size_button(renderer, 8);

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderLine(renderer, toolbar_button_offset, 0, toolbar_button_offset,
                   TOOLBAR_HEIGHT);
    toolbar_button_offset += TOOLBAR_MARGIN;

    SDL_SetRenderDrawColor(renderer, 150, 150, 150, SDL_ALPHA_OPAQUE);
    current_color_rect = (SDL_FRect){toolbar_button_offset + TOOLBAR_MARGIN,
                                     (TOOLBAR_HEIGHT / 4), TOOLBAR_HEIGHT / 2,
                                     TOOLBAR_HEIGHT / 2};
    struct SDL_FRect palette_bg = {
        toolbar_button_offset, TOOLBAR_MARGIN,
        (TOOLBAR_HEIGHT / 4) * 10 + (3 * TOOLBAR_MARGIN) + current_color_rect.w,
        TOOLBAR_HEIGHT - (2 * TOOLBAR_MARGIN)};
    SDL_RenderFillRect(renderer, &palette_bg);
    toolbar_button_offset += TOOLBAR_MARGIN;

    SDL_SetRenderDrawColor(renderer, state.color.r, state.color.g,
                           state.color.b, SDL_ALPHA_OPAQUE);
    SDL_RenderFillRect(renderer, &current_color_rect);
    toolbar_button_offset += current_color_rect.w;
    toolbar_button_offset += TOOLBAR_MARGIN;
    for (size_t i = 0; i < 20; i++) {
        new_palette_button(renderer, color_from_string(palette_colors[i]),
                           i % 2);
    }

    SDL_SetRenderTarget(renderer, NULL);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    // SDL will clean up the window/renderer for us
    SDL_DestroyTexture(canvas_texture);
    SDL_DestroyTexture(canvas_texture_preview);
    SDL_DestroyTexture(toolbar_texture);
    free_buttons();
}