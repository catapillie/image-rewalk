#include <stdlib.h>
#include <getopt.h>
#include <stdio.h>

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>

typedef struct work
{
    int img_x;
    int img_y;
    int color_x;
    int color_y;

    struct work *next;
} work_t;

work_t *work_cons(
    int img_x, int img_y,
    int color_x, int color_y,
    work_t *next)
{
    work_t *w = malloc(sizeof(work_t));
    w->img_x = img_x;
    w->img_y = img_y;
    w->color_x = color_x;
    w->color_y = color_y;
    w->next = next;
    return w;
}

work_t *work_pop(work_t *w)
{
    if (w == NULL)
        return NULL;

    work_t *next = w->next;
    free(w);
    return next;
}

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;

static SDL_Surface *source_image = NULL;
static SDL_Surface *dest_image = NULL;
static bool *dest_is_set = NULL;
static SDL_Texture *dest_texture = NULL;

#define DIRECTIONS_COUNT 8
static int dir_x[DIRECTIONS_COUNT] = {+0, +1, +1, +1, +0, -1, -1, -1};
static int dir_y[DIRECTIONS_COUNT] = {+1, +1, +0, -1, -1, -1, +0, +1};

static work_t *current_work = NULL;

static int width = 600;
static int height = 600;
static char *output = "out.png";
static bool tiled = false;
static bool is_preview = false;

void process_work()
{
    if (current_work == NULL)
        return;

    work_t step = *current_work;
    current_work = work_pop(current_work);

    if (step.img_x < 0 || step.img_x >= width || step.img_y < 0 || step.img_y >= height)
        return;

    if (dest_is_set[step.img_x + step.img_y * width])
        return;
    dest_is_set[step.img_x + step.img_y * width] = true;

    Uint8 r, g, b, a;
    SDL_ReadSurfacePixel(source_image, step.color_x, step.color_y, &r, &g, &b, &a);
    SDL_WriteSurfacePixel(dest_image, step.img_x, step.img_y, r, g, b, a);

    // random flood fill
    int perm[] = {0, 1, 2, 3, 4, 5, 6, 7};
    for (int i = 0; i < DIRECTIONS_COUNT - 1; i++)
    {
        int j = i + SDL_rand(DIRECTIONS_COUNT - i);
        int tmp = perm[i];
        perm[i] = perm[j];
        perm[j] = tmp;
    }

    for (int i = 0; i < DIRECTIONS_COUNT; i++)
    {
        int ox = dir_x[perm[i]];
        int oy = dir_y[perm[i]];
        int new_img_x = step.img_x + ox;
        int new_img_y = step.img_y + oy;

        if (tiled)
        {
            while (new_img_x < 0)
                new_img_x += width;
            while (new_img_x >= width)
                new_img_x -= width;
            while (new_img_y < 0)
                new_img_y += height;
            while (new_img_y >= height)
                new_img_y -= height;
        }

        // walk across image, avoid leaving bounds
        int color_x, color_y;
        do
        {
            int choice = SDL_rand(8);
            color_x = step.color_x + dir_x[choice];
            color_y = step.color_y + dir_y[choice];
        } while (color_x < 0 || color_x >= source_image->w || color_y < 0 || color_y >= source_image->h);

        current_work = work_cons(new_img_x, new_img_y, color_x, color_y, current_work);
    }
}

SDL_AppResult generate_rewalked_image()
{
    fprintf(stderr, "Generating new rewalked image... ");
    fflush(stderr);

    dest_is_set = SDL_calloc(width * height, sizeof(bool));

    current_work = work_cons(width / 2, height / 2, source_image->w / 2, source_image->h / 2, current_work);
    while (current_work != NULL)
        process_work();

    SDL_free(dest_is_set);

    // upload result to GPU
    dest_texture = SDL_CreateTextureFromSurface(renderer, dest_image);
    if (dest_texture == NULL)
    {
        SDL_Log("Failed to upload resulting image to GPU: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    fprintf(stderr, "done!\n");
    return SDL_APP_CONTINUE;
}

SDL_AppResult save_image()
{
    if (dest_texture == NULL)
    {
        fprintf(stderr, "Nothing to save yet.\n");
        return SDL_APP_CONTINUE;
    }

    fprintf(stderr, "Saving to '%s'... ", output);
    fflush(stderr);

    if (!IMG_SavePNG(dest_image, output))
    {
        SDL_Log("Couldn't write result to image file: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    fprintf(stderr, "done!\n");
    return SDL_APP_CONTINUE;
}

const struct option long_options[] = {
    {.name = "width", .has_arg = optional_argument, .flag = NULL, .val = 'w'},
    {.name = "height", .has_arg = optional_argument, .flag = NULL, .val = 'h'},
    {.name = "output", .has_arg = optional_argument, .flag = NULL, .val = 'o'},
    {.name = "tiled", .has_arg = optional_argument, .flag = NULL, .val = 't'},
    {.name = "preview", .has_arg = optional_argument, .flag = NULL, .val = 'p'},
};

void print_help()
{
    fprintf(stderr, "DESCRIPTION:\n");
    fprintf(stderr, "    Transform an input image into another using random walks\n");
    fprintf(stderr, "USAGE:\n");
    fprintf(stderr, "    rewalker [options...] <path_to_img>\n");
    fprintf(stderr, "OPTIONS:\n");
    fprintf(stderr, "    --output=...     (string)     Sets the output path for the generated image\n");
    fprintf(stderr, "    --width=...      (int)        The width of the generated image in px. Default is 600px\n");
    fprintf(stderr, "    --height=...     (int)        The height of the generated image in px. Default is 600px\n");
    fprintf(stderr, "    --tiled                       Attempts to generate an image that tiles on both axes\n");
    fprintf(stderr, "    --preview                     Opens a window to preview generated images, and generate more\n");
    fprintf(stderr, "EXAMPLE:\n");
    fprintf(stderr, "    rewalker --width=1920 --height=1080 --output=wallpaper.png source.png\n");
}

SDL_AppResult SDL_AppInit(void **state, int argc, char *argv[])
{
    char opt;
    while ((opt = getopt_long(argc, argv, "", long_options, NULL)) != -1)
    {
        switch (opt)
        {

        case 'w':
            if (optarg == NULL)
            {
                fprintf(stderr, "'width' option requires a value\n");
                return SDL_APP_FAILURE;
            }
            width = atoi(optarg);
            if (width <= 0)
            {
                fprintf(stderr, "'width' option must be a non-zero positive integer\n");
                return SDL_APP_FAILURE;
            }
            break;

        case 'h':
            if (optarg == NULL)
            {
                fprintf(stderr, "'height' option requires a value\n");
                return SDL_APP_FAILURE;
            }
            height = atoi(optarg);
            if (height <= 0)
            {
                fprintf(stderr, "'height' option must be a non-zero positive integer\n");
                return SDL_APP_FAILURE;
            }
            break;

        case 'o':
            if (optarg == NULL)
            {
                fprintf(stderr, "'o' option requires a value\n");
                return SDL_APP_FAILURE;
            }
            output = optarg;
            break;

        case 'p':
            is_preview = true;
            break;

        case 't':
            tiled = true;
            break;

        default:
            print_help();
            return SDL_APP_FAILURE;
        }
    }

    if (optind >= argc)
    {
        print_help();
        return SDL_APP_FAILURE;
    }

    char *path = argv[optind];
    fprintf(stderr, "Configuration:\n");
    fprintf(stderr, "    source path: '%s'\n", path);
    fprintf(stderr, "    output path: '%s'\n", output);
    fprintf(stderr, "    resolution: %dx%d\n", width, height);
    fprintf(stderr, "    tiled: %s\n", tiled ? "YES" : "NO");
    fprintf(stderr, "\n");

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("Failed to initialize SDL3: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_CreateWindowAndRenderer("Image rewalker", width, height, 0, &window, &renderer))
    {
        SDL_Log("Failed to create window/renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    source_image = IMG_Load(path);
    if (source_image == NULL)
    {
        SDL_Log("Failed to load input image: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    dest_image = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA8888);
    if (dest_image == NULL)
    {
        SDL_Log("Failed to allocate output image: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_AppResult result = generate_rewalked_image();
    if (result != SDL_APP_CONTINUE)
        return result;

    if (is_preview)
    {
        fprintf(stderr, "COMMANDS IN PREVIEW MODE:\n");
        fprintf(stderr, "    * <enter>  : run the rewalk\n");
        fprintf(stderr, "    * <space>  : save the current result\n");
        fprintf(stderr, "    * <escape> : exit\n");
        fprintf(stderr, "\n");
    }
    else
    {
        result = save_image();
        if (result != SDL_APP_CONTINUE)
            return result;

        return SDL_APP_SUCCESS;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *state, SDL_Event *event)
{
    switch (event->type)
    {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;

    case SDL_EVENT_KEY_DOWN:
        switch (event->key.key)
        {
        case SDLK_ESCAPE:
            return SDL_APP_SUCCESS;

        case SDLK_RETURN:
            SDL_AppResult result = generate_rewalked_image();
            if (result != SDL_APP_CONTINUE)
                return result;
            break;

        case SDLK_SPACE:
            return save_image();

        default:
            break;
        }
        break;

    default:
        break;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    if (dest_texture != NULL)
    {
        SDL_FRect dst = {.x = 0.0, .y = 0.0, .w = width, .h = height};
        SDL_RenderTexture(renderer, dest_texture, NULL, &dst);
    }

    SDL_RenderPresent(renderer);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *state, SDL_AppResult result)
{
    SDL_DestroySurface(source_image);
    SDL_DestroySurface(dest_image);

    if (dest_texture != NULL)
        SDL_DestroyTexture(dest_texture);
}
