/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015 K. Lange
 *
 * pong - Window Manager Pong
 *
 * Play pong where the paddles and ball are all windows.
 * Use the WM bindings to drag the left paddle to play.
 * Press `q` to quit.
 *
 * Rendering updates are all done by the compositor, while the game
 * only renders to the windows once at start up.
 *
 * Window movement tracking keeps the game logic aware of the paddle
 * position, and window moves for the ball and other paddle keep
 * things in the right place visually.
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <math.h>
#include <sched.h>
#include <sys/time.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>

#define GAME_PATH "/usr/share/pong"

#define PADDLE_WIDTH  50
#define PADDLE_HEIGHT 300
#define BALL_SIZE     50

static yutani_t * yctx;
static int spkr = 0;

struct object {
	double x;
	double y;
	int width;
	int height;
	double vel_x;
	double vel_y;
	sprite_t sprite;
};

static yutani_window_t * paddle_left;
static yutani_window_t * paddle_right;
static yutani_window_t * ball_win;

static struct object left;
static struct object right;
static struct object ball;

static gfx_context_t * paddle_left_ctx;
static gfx_context_t * paddle_right_ctx;
static gfx_context_t * ball_ctx;

static int should_exit = 0;

static int left_score = 0;
static int right_score = 0;

struct spkr {
	int length;
	int frequency;
};

static void note(int frequency) {
	struct spkr s = {
		.length = 2,
		.frequency = frequency,
	};

	write(spkr, &s, sizeof(s));
}


static uint32_t current_time() {
	static uint32_t start_time = 0;
	static uint32_t start_subtime = 0;

	struct timeval t;
	gettimeofday(&t, NULL);

	if (!start_time) {
		start_time = t.tv_sec;
		start_subtime = t.tv_usec;
	}

	uint32_t sec_diff = t.tv_sec - start_time;
	uint32_t usec_diff = t.tv_usec - start_subtime;

	if (t.tv_usec < (int)start_subtime) {
		sec_diff -= 1;
		usec_diff = (1000000 + t.tv_usec) - start_subtime;
	}

	return (uint32_t)(sec_diff * 1000 + usec_diff / 1000);
}

static int colliding(struct object * a, struct object * b) {
	if (a->x >= b->x + b->width) return 0;
	if (a->y >= b->y + b->height) return 0;
	if (b->x >= a->x + a->width) return 0;
	if (b->y >= a->y + a->height) return 0;
	return 1;
}


void redraw(void) {
	draw_fill(paddle_left_ctx, rgba(0,0,0,0));
	draw_fill(paddle_right_ctx, rgba(0,0,0,0));
	draw_fill(ball_ctx, rgba(0,0,0,0));

	draw_sprite(paddle_left_ctx, &left.sprite, 0, 0);
	draw_sprite(paddle_right_ctx, &right.sprite, 0, 0);
	draw_sprite(ball_ctx, &ball.sprite, 0, 0);

	yutani_flip(yctx, paddle_left);
	yutani_flip(yctx, paddle_right);
	yutani_flip(yctx, ball_win);
}

void update_left(void) {
	yutani_window_move(yctx, paddle_left, left.x, left.y);
}
void update_right(void) {
	yutani_window_move(yctx, paddle_right, right.x, right.y);
}
void update_ball(void) {
	yutani_window_move(yctx, ball_win, ball.x, ball.y);
}

void update_stuff(void) {

	right.vel_y = (right.y + right.height / 2 < ball.y + ball.height / 2) ? 2.0 : -2.0;
	right.y += right.vel_y;
	update_right();

	ball.x += ball.vel_x;
	ball.y += ball.vel_y;

	if (ball.y < 0) {
		ball.vel_y = -ball.vel_y;
		ball.y = 0;
	}
	if (ball.y > yctx->display_height - ball.height) {
		ball.vel_y = -ball.vel_y;
		ball.y = yctx->display_height - ball.height;
	}

	if (ball.x < 0) {
		ball.x       = yctx->display_width / 2 - ball.width / 2;
		ball.y       = yctx->display_height / 2 - ball.height / 2;
		ball.vel_x   = -10.0;
		ball.vel_y = ((double)rand() / RAND_MAX) * 6.0 - 3.0;
		note(10000);
		right_score++;
		printf("%d : %d\n", left_score, right_score);
	}

	if (ball.x > yctx->display_width - ball.width ) {
		ball.x       = yctx->display_width / 2 - ball.width / 2;
		ball.y       = yctx->display_height / 2 - ball.height / 2;
		ball.vel_x   = 10.0;
		ball.vel_y = ((double)rand() / RAND_MAX) * 6.0 - 3.0;
		note(17000);
		left_score++;
		printf("%d : %d\n", left_score, right_score);
	}

	if (colliding(&ball, &left)) {
		ball.x = left.x + left.width + 2;
		ball.vel_x   = (abs(ball.vel_x) < 8.0) ? -ball.vel_x * 1.05 : -ball.vel_x;

		double intersect = ((ball.y + ball.height/2) - (left.y)) / ((double)left.height) - 0.5;
		ball.vel_y = intersect * 8.0;
		note(15680);
	}

	if (colliding(&ball, &right)) {
		ball.x = right.x - ball.width - 2;
		ball.vel_x   = (abs(ball.vel_x) < 8.0) ? -ball.vel_x * 1.05 : -ball.vel_x;

		double intersect = ((ball.y + ball.height/2) - (right.y)) / ((double)right.height/2.0);
		ball.vel_y = intersect * 3.0;
		note(11747);
	}

	update_ball();
}

int main (int argc, char ** argv) {

	yctx = yutani_init();

	left.width   = PADDLE_WIDTH;
	left.height  = PADDLE_HEIGHT;

	right.width  = PADDLE_WIDTH;
	right.height = PADDLE_HEIGHT;

	ball.width   = BALL_SIZE;
	ball.height  = BALL_SIZE;

	ball.x       = yctx->display_width / 2 - ball.width / 2;
	ball.y       = yctx->display_height / 2 - ball.height / 2;

	left.x       = 10;
	left.y       = yctx->display_height / 2 - left.height / 2;

	right.x      = yctx->display_width - right.width - 10;
	right.y      = yctx->display_height / 2 - right.height / 2;

	paddle_left  = yutani_window_create(yctx, PADDLE_WIDTH, PADDLE_HEIGHT);
	paddle_right = yutani_window_create(yctx, PADDLE_WIDTH, PADDLE_HEIGHT);
	ball_win     = yutani_window_create(yctx, BALL_SIZE, BALL_SIZE);

	paddle_left_ctx  = init_graphics_yutani(paddle_left);
	paddle_right_ctx = init_graphics_yutani(paddle_right);
	ball_ctx         = init_graphics_yutani(ball_win);

	srand(time(NULL));

	ball.vel_y = ((double)rand() / RAND_MAX) * 6.0 - 3.0;
	ball.vel_x = -10.0;

	fprintf(stderr, "Loading sprites...\n");
	load_sprite(&left.sprite, GAME_PATH "/paddle-red.png");
	load_sprite(&right.sprite,GAME_PATH "/paddle-blue.png");
	load_sprite(&ball.sprite, GAME_PATH "/ball.png");

	redraw();
	update_left();
	update_right();
	update_ball();

	uint32_t last_tick = current_time();

	spkr = open("/dev/spkr", O_WRONLY);

	while (!should_exit) {
		uint32_t t = current_time();
		if (t > last_tick + 10) {
			last_tick += 10;
			update_stuff();
		}
		yutani_msg_t * m = yutani_poll_async(yctx);
		if (m) {
			switch (m->type) {
				case YUTANI_MSG_KEY_EVENT:
					{
						struct yutani_msg_key_event * ke = (void*)m->data;
						
						if (ke->event.key == 'q' && ke->event.action == KEY_ACTION_DOWN) {
							should_exit = 1;
						}
					}
					break;
				case YUTANI_MSG_WINDOW_MOVE:
					{
						struct yutani_msg_window_move * wm = (void*)m->data;
						if (wm->wid == paddle_left->wid) {
							/* Update paddle speed and position */
							left.y = (double)wm->y;
							if (wm->x != (int)left.x) {
								update_left();
							}
						}
					}
					break;
				case YUTANI_MSG_WINDOW_MOUSE_EVENT:
					{
						struct yutani_msg_window_mouse_event * me = (void*)m->data;
						if (me->command == YUTANI_MOUSE_EVENT_DOWN && me->buttons & YUTANI_MOUSE_BUTTON_LEFT) {
							if (me->wid == paddle_left->wid) {
								yutani_window_drag_start(yctx, paddle_left);
							}
						}
					}
					break;
				case YUTANI_MSG_SESSION_END:
					should_exit = 1;
					break;
				default:
					break;
			}
			free(m);
		} else {
			sched_yield();
		}
	}

	yutani_close(yctx, paddle_left);
	yutani_close(yctx, paddle_right);
	yutani_close(yctx, ball_win);

	return 0;
}
