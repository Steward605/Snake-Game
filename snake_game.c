#include <gtk/gtk.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sqlite3.h>

#define WINDOW_WIDTH 1200
#define WINDOW_HEIGHT 600
#define BLOCK_SIZE 40
#define MAX_BOMBS 5
#define FRAME_INTERVAL 5
#define MOVE_INTERVAL 5

typedef struct {
    int x, y;
} Point;

typedef struct {
    Point body[100];
    int length;
    Point direction;
} Snake;

typedef struct {
    Point position;
    int type;
} Food;

typedef struct {
    Point position;
} Bomb;

typedef struct {
    SDL_Window *sdl_window;
    SDL_Renderer *renderer;
    TTF_Font *font;
    Snake snake;
    Food food;

    // Array of bombs
    Bomb bombs[MAX_BOMBS];

    // Track if bombs are visible
    bool bomb_visible;

    // Timer to control bomb visibility
    int bomb_timer;
    
    int score;
    bool running;
    bool game_over;
    GtkWidget *start_window;
    GtkWidget *game_over_window;
    SDL_Texture *apple_texture;
    SDL_Texture *orange_texture;
    SDL_Texture *pear_texture;
    SDL_Texture *bomb_texture;
    int frame_counter;
    int move_timer;
    int best_score;
    sqlite3 *db;
} GameState;

// Function prototypes
bool is_snake_body(GameState *state, Point position);
void init_textures(GameState *state);
void init_game(GameState *state);
void handle_input(GameState *state);
void update_game(GameState *state);
void render_game(GameState *state);
void close_game(GameState *state);
void spawn_food(GameState *state);
void spawn_bombs(GameState *state);
bool check_collision(Point a, Point b);
static void on_return_to_menu(GtkWidget *widget, gpointer data);
static void on_play_again(GtkWidget *widget, gpointer data);
static void on_quit(GtkWidget *widget, gpointer data);

void init_database(sqlite3 **db) {
    int rc = sqlite3_open("snake_game.db", db);
    if (rc) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(*db));
        exit(1);
    }

    const char *sql = "CREATE TABLE IF NOT EXISTS BestScore ("
                      "id INTEGER PRIMARY KEY, "
                      "score INTEGER);";

    char *err_msg = NULL;
    rc = sqlite3_exec(*db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(*db);
        exit(1);
    }
}

int get_best_score(sqlite3 *db) {
    const char *sql = "SELECT MAX(score) FROM BestScore;";
    sqlite3_stmt *stmt;
    int best_score = 0;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            best_score = sqlite3_column_int(stmt, 0);
            printf("Best Score Retrieved: %d\n", best_score);
        }
    }
    sqlite3_finalize(stmt);
    return best_score;
}

void save_best_score(sqlite3 *db, int score) {
    const char *sql = "INSERT INTO BestScore (score) VALUES (?);";
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        printf("Saving Best Score: %d\n", score);
        sqlite3_bind_int(stmt, 1, score);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            fprintf(stderr, "Failed to save score: %s\n", sqlite3_errmsg(db));
        }
    }
    sqlite3_finalize(stmt);
}

// Prevent food overlap with snake
bool is_snake_body(GameState *state, Point position) {
    for (int i = 0; i < state->snake.length; i++) {
        if (check_collision(state->snake.body[i], position)) {
            return true;
        }
    }
    return false;
}

// Prevent overlapping of food and boomb position
void spawn_food(GameState *state) {
    bool overlap_with_bomb = false;
    do {
        state->food.position.x = rand() % (WINDOW_WIDTH / BLOCK_SIZE);
        state->food.position.y = rand() % (WINDOW_HEIGHT / BLOCK_SIZE);

        // Check if the food overlaps with any bomb when bombs are visible
        if (state->bomb_visible) {
            for (int i = 0; i < MAX_BOMBS; i++) {
                if (check_collision(state->food.position, state->bombs[i].position)) {
                    overlap_with_bomb = true;
                    break;
                }
            }
        }
    } while (overlap_with_bomb || is_snake_body(state, state->food.position));

    //Random food type
    state->food.type = rand() % 3; // 0 for apple, 1 for orange, 2 for pear
}

void spawn_bombs(GameState *state) {
    for (int i = 0; i < MAX_BOMBS; i++) {
        do {
            state->bombs[i].position.x = rand() % (WINDOW_WIDTH / BLOCK_SIZE);
            state->bombs[i].position.y = rand() % (WINDOW_HEIGHT / BLOCK_SIZE);
        } while (is_snake_body(state, state->bombs[i].position) || 
                 check_collision(state->bombs[i].position, state->food.position));
    }
}

void init_textures(GameState *state) {
    if (state->apple_texture) {
        SDL_DestroyTexture(state->apple_texture);
        state->apple_texture = NULL;
    }
    state->apple_texture = IMG_LoadTexture(state->renderer, "media/apple.png");
    if (!state->apple_texture) {
        fprintf(stderr, "Failed to load apple texture! SDL_image Error: %s\n", IMG_GetError());
    }
    state->orange_texture = IMG_LoadTexture(state->renderer, "media/orange.png");
    if (!state->orange_texture) {
        fprintf(stderr, "Failed to load orange texture! SDL_image Error: %s\n", IMG_GetError());
    }
    state->pear_texture = IMG_LoadTexture(state->renderer, "media/pear.png");
    if (!state->pear_texture) {
        fprintf(stderr, "Failed to load pear texture! SDL_image Error: %s\n", IMG_GetError());
    }
    if (state->bomb_texture) {
        SDL_DestroyTexture(state->bomb_texture);
        state->bomb_texture = NULL;
    }
    state->bomb_texture = IMG_LoadTexture(state->renderer, "media/bomb.png");
    if (!state->bomb_texture) {
        fprintf(stderr, "Failed to load bomb texture! SDL_image Error: %s\n", IMG_GetError());
    }
}

void init_game(GameState *state) {
    state->snake.length = 1;
    for (int i = 0; i < state->snake.length; i++) {
        state->snake.body[i].x = 10;
        state->snake.body[i].y = 10;
    }
    state->snake.direction.x = 1;
    state->snake.direction.y = 0;

    spawn_food(state);
    state->bomb_visible = false;
    state->bomb_timer = rand() % 100 + 50;
    state->running = true;
    state->game_over = false;
    state->move_timer = 0;

    state->score = 0;
    state->best_score = get_best_score(state->db);
    printf("Initial best score: %d\n", state->best_score);
}

void reinitialize_font(GameState *state) {
    if (state->font) {
        TTF_CloseFont(state->font);
        state->font = NULL;
    }
    state->font = TTF_OpenFont("media/arial.ttf", 24);
    if (!state->font) {
        fprintf(stderr, "Failed to load font! SDL_ttf Error: %s\n", TTF_GetError());
    }
}

bool check_collision(Point a, Point b) {
    return a.x == b.x && a.y == b.y;
}

void handle_input(GameState *state) {
    const Uint8 *keyboardState = SDL_GetKeyboardState(NULL);

    if (state->frame_counter % FRAME_INTERVAL == 0) {
        if (keyboardState[SDL_SCANCODE_UP] && state->snake.direction.y == 0) {
            state->snake.direction.x = 0;
            state->snake.direction.y = -1;
        } else if (keyboardState[SDL_SCANCODE_DOWN] && state->snake.direction.y == 0) {
            state->snake.direction.x = 0;
            state->snake.direction.y = 1;
        } else if (keyboardState[SDL_SCANCODE_LEFT] && state->snake.direction.x == 0) {
            state->snake.direction.x = -1;
            state->snake.direction.y = 0;
        } else if (keyboardState[SDL_SCANCODE_RIGHT] && state->snake.direction.x == 0) {
            state->snake.direction.x = 1;
            state->snake.direction.y = 0;
        }
    }
}

void update_game(GameState *state) {
    state->move_timer++;
    
    // Only move snake when timer reaches interval
    if (state->move_timer >= MOVE_INTERVAL) {
        state->move_timer = 0; 

        // Move the snake
        Point new_head = state->snake.body[0];
        new_head.x += state->snake.direction.x;
        new_head.y += state->snake.direction.y;

        // Check collisions with walls
        if (new_head.x < 0 || new_head.x >= WINDOW_WIDTH / BLOCK_SIZE ||
            new_head.y < 0 || new_head.y >= WINDOW_HEIGHT / BLOCK_SIZE) {
            state->running = false;
            state->game_over = true;
        }

        // Check collisions with itself
        for (int i = 0; i < state->snake.length; i++) {
            if (check_collision(new_head, state->snake.body[i])) {
                state->running = false;
                state->game_over = true;
            }
        }

        if (state->game_over) {
            close_game(state);
            gtk_window_present(GTK_WINDOW(state->game_over_window));
            return;
        }

        // Update snake body
        for (int i = state->snake.length - 1; i > 0; i--) {
            state->snake.body[i] = state->snake.body[i - 1];
        }
        state->snake.body[0] = new_head;

        // Check collision with food
        if (check_collision(new_head, state->food.position)) {
            state->snake.length++;
            state->snake.body[state->snake.length - 1] = state->snake.body[state->snake.length - 2];

            // Add score based on food type
            switch (state->food.type) {
                case 0: // Apple
                    state->score += 100;
                    break;
                case 1: // Orange
                    state->score += 200;
                    break;
                case 2: // Pear
                    state->score += 300;
                    break;
            }

            spawn_food(state);
        }

        // Bomb logic
        if (state->bomb_visible) {
            state->bomb_timer--;
            if (state->bomb_timer <= 0) {
                state->bomb_visible = false;
                state->bomb_timer = rand() % 100 + 50;
            }

            // Check collision with any bomb
            for (int i = 0; i < MAX_BOMBS; i++) {
                if (check_collision(new_head, state->bombs[i].position)) {
                    state->game_over = true;
                    gtk_window_present(GTK_WINDOW(state->game_over_window));
                    return;
                }
            }
        } else {
            state->bomb_timer--;
            if (state->bomb_timer <= 0) {
                spawn_bombs(state);
                state->bomb_visible = true;
                state->bomb_timer = 100;
            }
        }

        if (state->game_over) {
            if (state->score > state->best_score) {
                state->best_score = state->score;
                save_best_score(state->db, state->best_score);
            }
            gtk_window_present(GTK_WINDOW(state->game_over_window));
            return;
        }
    }
}

void render_game(GameState *state) {
    SDL_SetRenderDrawColor(state->renderer, 50, 50, 50, 255); // Dark grey background
    SDL_RenderClear(state->renderer);

    // Draw grid background with alternating colors
    for (int y = 0; y < WINDOW_HEIGHT / BLOCK_SIZE; y++) {
        for (int x = 0; x < WINDOW_WIDTH / BLOCK_SIZE; x++) {
            // Alternate colors based on grid position
            if ((x + y) % 2 == 0) {
                SDL_SetRenderDrawColor(state->renderer, 152, 251, 152, 255); // Mint Green
            } else {
                SDL_SetRenderDrawColor(state->renderer, 143, 188, 143, 255); // Dark Sea Green
            }

            SDL_Rect rect = {x * BLOCK_SIZE, y * BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE};
            SDL_RenderFillRect(state->renderer, &rect);
        }
    }

    // Draw snake
    SDL_SetRenderDrawColor(state->renderer, 100, 149, 237, 255);
    for (int i = 0; i < state->snake.length; i++) {
        SDL_Rect rect = {state->snake.body[i].x * BLOCK_SIZE, state->snake.body[i].y * BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE};
        SDL_RenderFillRect(state->renderer, &rect);
    }

    // Draw food
    SDL_Rect food_rect = {state->food.position.x * BLOCK_SIZE, state->food.position.y * BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE};
    switch (state->food.type) {
        case 0:
            SDL_RenderCopy(state->renderer, state->apple_texture, NULL, &food_rect);
            break;
        case 1:
            SDL_RenderCopy(state->renderer, state->orange_texture, NULL, &food_rect);
            break;
        case 2:
            SDL_RenderCopy(state->renderer, state->pear_texture, NULL, &food_rect);
            break;
    }

    // Render bomb if visible
    if (state->bomb_visible) {
        for (int i = 0; i < MAX_BOMBS; i++) {
            SDL_Rect bomb_rect = {
                state->bombs[i].position.x * BLOCK_SIZE,
                state->bombs[i].position.y * BLOCK_SIZE,
                BLOCK_SIZE,
                BLOCK_SIZE
            };
            SDL_RenderCopy(state->renderer, state->bomb_texture, NULL, &bomb_rect);
        }
    }

    // Render the score
    if (state->font) {
        SDL_Color color = {255, 255, 255, 255}; // White color
        char score_text[50];
        snprintf(score_text, sizeof(score_text), "Score: %d", state->score);

        SDL_Surface *surface = TTF_RenderText_Solid(state->font, score_text, color);
        SDL_Texture *texture = SDL_CreateTextureFromSurface(state->renderer, surface);

        // Black background for the score text
        SDL_SetRenderDrawColor(state->renderer, 0, 0, 0, 255);
        SDL_Rect score_background = {10, 10, surface->w + 10, surface->h + 10}; // Padding around text
        SDL_RenderFillRect(state->renderer, &score_background);

        SDL_Rect score_rect = {10, 10, surface->w, surface->h};
        SDL_RenderCopy(state->renderer, texture, NULL, &score_rect);

        SDL_FreeSurface(surface);
        SDL_DestroyTexture(texture);

        char best_score_text[50];
        snprintf(best_score_text, sizeof(best_score_text), "Best Score: %d", state->best_score);

        SDL_Surface *best_surface = TTF_RenderText_Solid(state->font, best_score_text, color);
        SDL_Texture *best_texture = SDL_CreateTextureFromSurface(state->renderer, best_surface);

        // Black background for the score text
        SDL_SetRenderDrawColor(state->renderer, 0, 0, 0, 255);
        SDL_Rect best_score_background = {10, 40, best_surface->w + 10, best_surface->h + 10}; // Padding around text
        SDL_RenderFillRect(state->renderer, &best_score_background);

        SDL_Rect best_score_rect = {10, 40, best_surface->w, best_surface->h};
        SDL_RenderCopy(state->renderer, best_texture, NULL, &best_score_rect);

        SDL_FreeSurface(best_surface);
        SDL_DestroyTexture(best_texture);

        SDL_RenderPresent(state->renderer);
    }
}

void close_game(GameState *state) {
    if (state->apple_texture) {
        SDL_DestroyTexture(state->apple_texture);
        state->apple_texture = NULL;
    }
    if (state->orange_texture) {
        SDL_DestroyTexture(state->orange_texture);
        state->orange_texture = NULL;
    }
    if (state->pear_texture) {
        SDL_DestroyTexture(state->pear_texture);
        state->pear_texture = NULL;
    }
    if (state->bomb_texture) {
        SDL_DestroyTexture(state->bomb_texture);
        state->bomb_texture = NULL;
    }
    if (state->renderer) {
        SDL_DestroyRenderer(state->renderer);
        state->renderer = NULL;
    }
    if (state->sdl_window) {
        SDL_DestroyWindow(state->sdl_window);
        state->sdl_window = NULL;
    }
    if (state->font) {
        TTF_CloseFont(state->font);
        state->font = NULL;
    }
    if (state->score > state->best_score) {
        state->best_score = state->score;
        save_best_score(state->db, state->best_score);
    }
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
}

// GTK callback functions for "Game Over" window
static void on_return_to_menu(GtkWidget *widget, gpointer data) {
    GameState *state = (GameState *)data; // Cast gpointer to GameState *
    gtk_widget_set_visible(state->game_over_window, FALSE);
    gtk_window_present(GTK_WINDOW(state->start_window));
}

static void on_play_again(GtkWidget *widget, gpointer data) {
    GameState *state = (GameState *)data;

    gtk_widget_set_visible(state->game_over_window, FALSE);
    
    // Destroy before reloading
    close_game(state);

    // Reload SDL state
    SDL_Init(SDL_INIT_VIDEO);
    TTF_Init();
    IMG_Init(IMG_INIT_PNG);
    reinitialize_font(state);

    state->sdl_window = SDL_CreateWindow("Snake Game", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    state->renderer = SDL_CreateRenderer(state->sdl_window, -1, SDL_RENDERER_ACCELERATED);

    init_textures(state);
    init_game(state);

    SDL_Event e;
    const Uint8 *keyboardState = SDL_GetKeyboardState(NULL);

    while (state->running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                state->running = false;
            }
        }

        state->frame_counter++;

        handle_input(state);

        if (!state->game_over) {
            update_game(state);
            render_game(state);
        } else {
            if (!state->running) {
                if (state->score > state->best_score) {
                    state->best_score = state->score;
                    save_best_score(state->db, state->best_score);
                }
            }
            gtk_window_present(GTK_WINDOW(state->game_over_window));
            break;
        }

        SDL_Delay(16);
    }

    close_game(state);
}

static void on_quit(GtkWidget *widget, gpointer data) {
    GameState *state = (GameState *)data;
    state->running = false;

    if (state->start_window != NULL) {
        gtk_window_destroy(GTK_WINDOW(state->start_window));
        state->start_window = NULL;
    }
    if (state->game_over_window != NULL) {
        gtk_window_destroy(GTK_WINDOW(state->game_over_window));
        state->game_over_window = NULL;
    }
}

// GTK "Game Over" window
static void create_game_over_window(GameState *state) {
    state->game_over_window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(state->game_over_window), "Game Over");
    gtk_window_set_default_size(GTK_WINDOW(state->game_over_window), 300, 200);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *label = gtk_label_new("Game Over!");
    GtkWidget *btn_return = gtk_button_new_with_label("Return to Menu");
    GtkWidget *btn_play_again = gtk_button_new_with_label("Play Again");
    GtkWidget *btn_quit = gtk_button_new_with_label("Quit");

    g_signal_connect(btn_return, "clicked", G_CALLBACK(on_return_to_menu), state);
    g_signal_connect(btn_play_again, "clicked", G_CALLBACK(on_play_again), state);
    g_signal_connect(btn_quit, "clicked", G_CALLBACK(on_quit), state);

    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), btn_return);
    gtk_box_append(GTK_BOX(box), btn_play_again);
    gtk_box_append(GTK_BOX(box), btn_quit);

    gtk_window_set_child(GTK_WINDOW(state->game_over_window), box);
    gtk_widget_set_visible(state->game_over_window, FALSE);
}

// GTK Start Menu setup
static void on_start_button_clicked(GtkWidget *widget, gpointer data) {
    GameState *state = (GameState *)data;

    gtk_widget_set_visible(state->start_window, FALSE); // Hide the start menu

    SDL_Init(SDL_INIT_VIDEO);
    TTF_Init();
    IMG_Init(IMG_INIT_PNG);

    state->font = TTF_OpenFont("media/arial.ttf", 24);

    // Create SDL window
    state->sdl_window = SDL_CreateWindow("Snake Game", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    state->renderer = SDL_CreateRenderer(state->sdl_window, -1, SDL_RENDERER_ACCELERATED);
    state->apple_texture = IMG_LoadTexture(state->renderer, "media/apple.png");
    state->bomb_texture = IMG_LoadTexture(state->renderer, "media/bomb.png");

    init_game(state);
    init_textures(state);

    SDL_Event e;
    const Uint8 *keyboardState = SDL_GetKeyboardState(NULL);

    while (state->running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                state->running = false;
            }
        }

        state->frame_counter++;

        handle_input(state);

        if (!state->game_over) {
            update_game(state);
            render_game(state);
        } else {
            if (!state->running) {
                if (state->score > state->best_score) {
                    state->best_score = state->score;
                    save_best_score(state->db, state->best_score);
                }
            }
            gtk_window_present(GTK_WINDOW(state->game_over_window));
            break;
        }

        SDL_Delay(16);
    }

    close_game(state);
}

static void on_quit_button_clicked(GtkWidget *widget, gpointer data) {
    GameState *state = (GameState *)data;
    state->running = false;

    if (state->start_window != NULL) {
        gtk_window_destroy(GTK_WINDOW(state->start_window));
        state->start_window = NULL;
    }
    if (state->game_over_window != NULL) {
        gtk_window_destroy(GTK_WINDOW(state->game_over_window));
        state->game_over_window = NULL;
    }
}

// GTK Application Activation
static void on_activate(GtkApplication *app, gpointer user_data) {
    GameState *state = (GameState *)user_data;

    state->start_window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(state->start_window), "Snake Game Menu");
    gtk_window_set_default_size(GTK_WINDOW(state->start_window), 300, 200);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *label = gtk_label_new("Welcome to Snake Game");
    GtkWidget *button_start = gtk_button_new_with_label("Start Game");
    GtkWidget *button_quit = gtk_button_new_with_label("Quit");

    g_signal_connect(button_start, "clicked", G_CALLBACK(on_start_button_clicked), state);
    g_signal_connect(button_quit, "clicked", G_CALLBACK(on_quit_button_clicked), state);

    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), button_start);
    gtk_box_append(GTK_BOX(box), button_quit);

    gtk_window_set_child(GTK_WINDOW(state->start_window), box);

    create_game_over_window(state); // Initialize the game-over window

    gtk_window_present(GTK_WINDOW(state->start_window));

    g_signal_connect(state->start_window, "destroy", G_CALLBACK(on_quit), state);
    g_signal_connect(state->game_over_window, "destroy", G_CALLBACK(on_quit), state);
}

int main(int argc, char *argv[]) {
    GameState state = {0}; // Stack allocation and initialization
    init_database(&(state.db));
    state.best_score = get_best_score(state.db);
    srand(time(NULL));

    GtkApplication *app = gtk_application_new("com.example.snake", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &state); // Pass state as user data

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
