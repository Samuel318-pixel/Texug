#include <stdint.h>

#include "arena.h"
#include "pit.h"
#include "video.h"

#define MAP_SIZE 10u
#define RAY_COUNT 64u
#define MAX_RAY_STEPS 192u
#define ENEMY_COUNT 2u
#define LEVEL_COUNT 2u

enum arena_state {
    ARENA_TITLE,
    ARENA_PLAYING,
    ARENA_WON,
    ARENA_LOST
};

struct arena_enemy {
    uint8_t x;
    uint8_t y;
    uint8_t health;
    uint8_t alive;
};

static const char worlds[LEVEL_COUNT][MAP_SIZE][MAP_SIZE + 1u] = {
    {
        "##########",
        "#........#",
        "#..#.....#",
        "#........#",
        "#.....#..#",
        "#........#",
        "#..#.....#",
        "#........#",
        "#........#",
        "##########",
    },
    {
        "##########",
        "#........#",
        "#....#...#",
        "#........#",
        "#..#.....#",
        "#........#",
        "#.....#..#",
        "#........#",
        "#........#",
        "##########",
    },
};

static const int8_t sine_quarter[17] = {
    0, 2, 3, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 15, 16, 16, 16,
};

static enum arena_state state;
static struct arena_enemy enemies[ENEMY_COUNT];
static uint8_t current_level;
static int32_t player_x;
static int32_t player_y;
static uint8_t player_direction;
static uint8_t player_health;
static uint8_t player_ammo;
static uint8_t enemies_killed;
static int32_t mouse_turn_remainder;
static uint32_t next_attack_tick;
static char last_action[12];

static int32_t direction_x(uint8_t angle)
{
    uint8_t quadrant = angle >> 4;
    uint8_t offset = angle & 15u;
    if (quadrant == 0u) {
        return sine_quarter[16u - offset];
    }
    if (quadrant == 1u) {
        return -(int32_t)sine_quarter[offset];
    }
    if (quadrant == 2u) {
        return -(int32_t)sine_quarter[16u - offset];
    }
    return sine_quarter[offset];
}

static int32_t direction_y(uint8_t angle)
{
    uint8_t quadrant = angle >> 4;
    uint8_t offset = angle & 15u;
    if (quadrant == 0u) {
        return sine_quarter[offset];
    }
    if (quadrant == 1u) {
        return sine_quarter[16u - offset];
    }
    if (quadrant == 2u) {
        return -(int32_t)sine_quarter[offset];
    }
    return -(int32_t)sine_quarter[16u - offset];
}

static uint32_t divide_u32(uint32_t numerator, uint32_t denominator)
{
    if (denominator == 0u) {
        return 0u;
    }
    uint32_t quotient = 0u;
    uint32_t remainder = 0u;
    for (int32_t bit = 31; bit >= 0; --bit) {
        remainder = (remainder << 1) | ((numerator >> bit) & 1u);
        if (remainder >= denominator) {
            remainder -= denominator;
            quotient |= 1u << bit;
        }
    }
    return quotient;
}

static int is_wall(int32_t x, int32_t y)
{
    if (x < 0 || y < 0) {
        return 1;
    }
    uint32_t cell_x = (uint32_t)x >> 8;
    uint32_t cell_y = (uint32_t)y >> 8;
    return cell_x >= MAP_SIZE || cell_y >= MAP_SIZE ||
           worlds[current_level - 1u][cell_y][cell_x] == '#';
}

static int enemy_at(int32_t x, int32_t y)
{
    if (x < 0 || y < 0) {
        return -1;
    }
    uint32_t cell_x = (uint32_t)x >> 8;
    uint32_t cell_y = (uint32_t)y >> 8;
    for (uint32_t index = 0u; index < ENEMY_COUNT; ++index) {
        if (enemies[index].alive && enemies[index].x == cell_x &&
            enemies[index].y == cell_y) {
            return (int)index;
        }
    }
    return -1;
}

static void reset_level(void)
{
    player_x = 384;
    player_y = 384;
    player_direction = 0u;
    player_health = 100u;
    player_ammo = 12u;
    enemies_killed = 0u;
    mouse_turn_remainder = 0;
    if (current_level == 1u) {
        enemies[0] = (struct arena_enemy){6u, 1u, 3u, 1u};
        enemies[1] = (struct arena_enemy){4u, 7u, 3u, 1u};
    } else {
        enemies[0] = (struct arena_enemy){7u, 3u, 3u, 1u};
        enemies[1] = (struct arena_enemy){2u, 7u, 3u, 1u};
    }
    next_attack_tick = pit_ticks_read() + 200u;
    last_action[0] = '\0';
}

void arena_initialize(void)
{
    state = ARENA_TITLE;
    current_level = 1u;
    reset_level();
}

static void move_player(int32_t dx, int32_t dy)
{
    int32_t next_x = player_x + dx;
    int32_t next_y = player_y + dy;
    if (!is_wall(next_x, player_y) &&
        enemy_at(next_x, player_y) < 0) {
        player_x = next_x;
    }
    if (!is_wall(player_x, next_y) &&
        enemy_at(player_x, next_y) < 0) {
        player_y = next_y;
    }
}

static void update_enemies(void)
{
    uint32_t ticks = pit_ticks_read();
    if ((int32_t)(ticks - next_attack_tick) < 0) {
        return;
    }
    next_attack_tick = ticks + 200u;
    for (uint32_t index = 0u; index < ENEMY_COUNT; ++index) {
        if (!enemies[index].alive) {
            continue;
        }
        int32_t enemy_x = (int32_t)enemies[index].x * 256 + 128;
        int32_t enemy_y = (int32_t)enemies[index].y * 256 + 128;
        int32_t delta_x = enemy_x - player_x;
        int32_t delta_y = enemy_y - player_y;
        if (delta_x < 0) {
            delta_x = -delta_x;
        }
        if (delta_y < 0) {
            delta_y = -delta_y;
        }
        if (delta_x < 320 && delta_y < 320 && player_health > 0u) {
            player_health = player_health > 10u
                                ? (uint8_t)(player_health - 10u)
                                : 0u;
            if (player_health == 0u) {
                state = ARENA_LOST;
                return;
            }
        }
    }
}

void arena_fire(void)
{
    if (state != ARENA_PLAYING) {
        return;
    }
    if (player_ammo == 0u) {
        last_action[0] = 'E';
        last_action[1] = 'M';
        last_action[2] = 'P';
        last_action[3] = 'T';
        last_action[4] = 'Y';
        last_action[5] = '\0';
        return;
    }
    --player_ammo;
    for (uint32_t step = 1u; step <= MAX_RAY_STEPS; ++step) {
        int32_t hit_x = player_x +
                        direction_x(player_direction) * (int32_t)step;
        int32_t hit_y = player_y +
                        direction_y(player_direction) * (int32_t)step;
        if (is_wall(hit_x, hit_y)) {
            break;
        }
        int enemy = enemy_at(hit_x, hit_y);
        if (enemy >= 0) {
            struct arena_enemy *target = &enemies[(uint32_t)enemy];
            if (target->health > 0u) {
                --target->health;
            }
            if (target->health == 0u) {
                target->alive = 0u;
                ++enemies_killed;
                if (enemies_killed == ENEMY_COUNT) {
                    state = ARENA_WON;
                }
            }
            last_action[0] = 'H';
            last_action[1] = 'I';
            last_action[2] = 'T';
            last_action[3] = '\0';
            return;
        }
    }
    last_action[0] = 'M';
    last_action[1] = 'I';
    last_action[2] = 'S';
    last_action[3] = 'S';
    last_action[4] = '\0';
}

void arena_handle_key(char key)
{
    if (key == '\n' && state != ARENA_PLAYING) {
        if (state == ARENA_WON) {
            current_level = current_level < LEVEL_COUNT
                                ? (uint8_t)(current_level + 1u)
                                : 1u;
        } else if (state == ARENA_TITLE) {
            current_level = 1u;
        }
        reset_level();
        state = ARENA_PLAYING;
        return;
    }
    if (state != ARENA_PLAYING) {
        return;
    }

    int32_t forward_x = direction_x(player_direction) * 3;
    int32_t forward_y = direction_y(player_direction) * 3;
    int32_t side_x = -direction_y(player_direction) * 3;
    int32_t side_y = direction_x(player_direction) * 3;
    if (key == 'w' || key == 'W') {
        move_player(forward_x, forward_y);
    } else if (key == 's' || key == 'S') {
        move_player(-forward_x, -forward_y);
    } else if (key == 'a' || key == 'A') {
        move_player(-side_x, -side_y);
    } else if (key == 'd' || key == 'D') {
        move_player(side_x, side_y);
    } else if (key == 'q' || key == 'Q') {
        player_direction = (uint8_t)((player_direction - 1u) & 63u);
    } else if (key == 'e' || key == 'E') {
        player_direction = (uint8_t)((player_direction + 1u) & 63u);
    } else if (key == 'f' || key == 'F' || key == ' ') {
        arena_fire();
    } else if (key == 'r' || key == 'R') {
        player_ammo = 12u;
    }
}

void arena_handle_mouse(int32_t delta_x)
{
    if (state != ARENA_PLAYING) {
        return;
    }
    mouse_turn_remainder += delta_x;
    while (mouse_turn_remainder >= 4) {
        player_direction = (uint8_t)((player_direction + 1u) & 63u);
        mouse_turn_remainder -= 4;
    }
    while (mouse_turn_remainder <= -4) {
        player_direction = (uint8_t)((player_direction - 1u) & 63u);
        mouse_turn_remainder += 4;
    }
}

static void draw_number(int32_t x, int32_t y, uint32_t value, uint8_t color)
{
    char digits[4];
    uint32_t count = 0u;
    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u && count < sizeof(digits));
    while (count != 0u) {
        char character[2] = {digits[--count], '\0'};
        video_draw_text(x, y, character, color);
        x += 6;
    }
}

static void draw_title(int32_t x, int32_t y, uint32_t width,
                       uint32_t height)
{
    video_fill_rect(x, y, width, height, 1u);
    video_draw_text(x + 24, y + 10, "TEXUGOS ARENA", 15u);
    video_draw_text(x + 22, y + 34, "ENTER TO START", 11u);
    video_draw_text(x + 16, y + 54, "WASD MOVE Q E TURN", 15u);
    video_draw_text(x + 16, y + 69, "F FIRE R RELOAD", 15u);
    video_draw_text(x + 16, y + 84, "MOUSE AIM CLICK FIRE", 15u);
}

void arena_draw(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    if (state == ARENA_TITLE) {
        draw_title(x, y, width, height);
        return;
    }
    if (state == ARENA_WON || state == ARENA_LOST) {
        video_fill_rect(x, y, width, height, 1u);
        video_draw_text(x + 28, y + 24,
                        state == ARENA_WON
                            ? (current_level == LEVEL_COUNT
                                   ? "GAME COMPLETE"
                                   : "LEVEL CLEAR")
                            : "GAME OVER",
                        state == ARENA_WON ? 10u : 4u);
        video_draw_text(x + 18, y + 48,
                        state == ARENA_WON && current_level < LEVEL_COUNT
                            ? "ENTER NEXT LEVEL"
                            : "ENTER RESTART",
                        15u);
        video_draw_text(x + 18, y + 68, "ESC DESKTOP", 15u);
        return;
    }

    update_enemies();
    if (state != ARENA_PLAYING) {
        arena_draw(x, y, width, height);
        return;
    }

    uint32_t view_height = height > 36u ? height - 34u : height;
    uint32_t horizon = view_height / 2u;
    video_fill_rect(x, y, width, horizon, 1u);
    video_fill_rect(x, y + (int32_t)horizon, width,
                    view_height - horizon, 8u);

    for (uint32_t column = 0u; column < RAY_COUNT; ++column) {
        int32_t angle_offset = (int32_t)(column >> 2) - 8;
        uint8_t angle = (uint8_t)((player_direction + angle_offset) & 63);
        int32_t ray_x = player_x;
        int32_t ray_y = player_y;
        int32_t step_x = direction_x(angle);
        int32_t step_y = direction_y(angle);
        uint32_t distance = MAX_RAY_STEPS;
        uint8_t wall_color = 9u;
        for (uint32_t step = 1u; step <= MAX_RAY_STEPS; ++step) {
            ray_x += step_x;
            ray_y += step_y;
            if (is_wall(ray_x, ray_y)) {
                distance = step;
                wall_color = step < 32u ? 15u : (step < 72u ? 11u : 9u);
                break;
            }
            if (enemy_at(ray_x, ray_y) >= 0) {
                distance = step;
                wall_color = 4u;
                break;
            }
        }
        uint32_t wall_height = divide_u32(view_height * 32u, distance);
        if (wall_height > view_height) {
            wall_height = view_height;
        }
        uint32_t top = horizon > wall_height / 2u
                           ? horizon - wall_height / 2u
                           : 0u;
        uint32_t left = column * width / RAY_COUNT;
        uint32_t right = (column + 1u) * width / RAY_COUNT;
        video_fill_rect(x + (int32_t)left, y + (int32_t)top,
                        right - left, wall_height, wall_color);
    }

    video_fill_rect(x + (int32_t)width / 2 - 3,
                    y + (int32_t)horizon - 1, 7u, 1u, 15u);
    video_fill_rect(x + (int32_t)width / 2,
                    y + (int32_t)horizon - 3, 1u, 5u, 15u);
    video_fill_rect(x + (int32_t)width / 2 - 7,
                    y + (int32_t)view_height - 13, 14u, 7u, 6u);
    video_draw_text(x + 2, y + (int32_t)view_height + 2, "HP", 1u);
    draw_number(x + 14, y + (int32_t)view_height + 2,
                player_health, 1u);
    video_draw_text(x + 42, y + (int32_t)view_height + 2, "AMMO", 1u);
    draw_number(x + 72, y + (int32_t)view_height + 2,
                player_ammo, 1u);
    video_draw_text(x + 108, y + (int32_t)view_height + 2, "K", 1u);
    draw_number(x + 120, y + (int32_t)view_height + 2,
                enemies_killed, 1u);
    video_draw_text(x + 138, y + (int32_t)view_height + 2, "L", 1u);
    draw_number(x + 150, y + (int32_t)view_height + 2,
                current_level, 1u);
    if (last_action[0] != '\0') {
        video_draw_text(x + (int32_t)width - 30,
                        y + (int32_t)view_height + 2, last_action, 4u);
    }
}
