#include <stdio.h>

#include <toaru/graphics.h>
#include <toaru/png.h>

int main(int argc, char * argv[]) {
	sprite_t sprite;
	return load_sprite_png(&sprite,"/opt/logo_login.png");
}
