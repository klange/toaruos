/**
 * @brief Panel Network Status Widget
 */
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <arpa/inet.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/menu.h>
#include <toaru/text.h>
#include <toaru/panel.h>
static int netstat_count = 0;
static int netstat_prev[32] = {0};
static char netstat_data[32][1024];
static struct MenuList * netstat;
static int network_status = 0;
static sprite_t * sprite_net_active;
static sprite_t * sprite_net_disabled;

static void ip_ntoa(const uint32_t src_addr, char * out) {
	snprintf(out, 16, "%d.%d.%d.%d",
		(src_addr & 0xFF000000) >> 24,
		(src_addr & 0xFF0000) >> 16,
		(src_addr & 0xFF00) >> 8,
		(src_addr & 0xFF));
}

static void netif_show_toast(const char * str) {
	int toastDaemon = open("/dev/pex/toast", O_WRONLY);
	if (toastDaemon >= 0) {
		write(toastDaemon, str, strlen(str));
		close(toastDaemon);
	}
}

static void netif_disconnected(const char * if_name) {
	network_status |= 1;
	if (netstat_prev[netstat_count] && netstat_prev[netstat_count] != 1) {
		char toastMsg[1024];
		sprintf(toastMsg, "{\"icon\":\"/usr/share/icons/48/network-jack-disconnected.png\",\"body\":\"<b>%s</b><br>Network disconnected.\"}", if_name);
		netif_show_toast(toastMsg);
	}
	netstat_prev[netstat_count] = 1;
}

static void netif_connected(const char * if_name) {
	network_status |= 2;
	if (netstat_prev[netstat_count] && netstat_prev[netstat_count] != 2) {
		char toastMsg[1024];
		sprintf(toastMsg, "{\"icon\":\"/usr/share/icons/48/network-jack.png\",\"body\":\"<b>%s</b><br>Connection established.\"}", if_name);
		netif_show_toast(toastMsg);
	}
	netstat_prev[netstat_count] = 2;
}

static void check_network(const char * if_name) {
	if (netstat_count >= 32) return;

	char if_path[512];
	snprintf(if_path, 511, "/dev/net/%s", if_name);
	int netdev = open(if_path, O_RDONLY);

	if (netdev < 0) return;

	uint32_t flags;
	if (!ioctl(netdev, SIOCGIFFLAGS, &flags)) {
		if (!(flags & IFF_UP)) {
			snprintf(netstat_data[netstat_count], 1023, "%s: disconnected", if_name);
			netif_disconnected(if_name);
			goto _netif_next;
		}
	}

	/* Get IPv4 address */
	uint32_t ip_addr;
	if (!ioctl(netdev, SIOCGIFADDR, &ip_addr)) {
		char ip_str[16];
		ip_ntoa(ntohl(ip_addr), ip_str);
		snprintf(netstat_data[netstat_count], 1023, "%s: %s", if_name, ip_str);
		netif_connected(if_name);
	} else {
		snprintf(netstat_data[netstat_count], 1023, "%s: No address", if_name);
		netif_disconnected(if_name);
	}

_netif_next:
	close(netdev);
	netstat_count++;
}

static int widget_update_network(struct PanelWidget * this, int * force_updates) {
	network_status = 0;
	netstat_count = 0;

	DIR * d = opendir("/dev/net");
	if (!d) return 1;

	struct dirent * ent;
	while ((ent = readdir(d))) {
		if (ent->d_name[0] == '.') continue;
		if (!strcmp(ent->d_name, "lo")) continue; /* Ignore loopback */
		check_network(ent->d_name);
	}

	closedir(d);
	return 0;
}

static int widget_click_network(struct PanelWidget * this, struct yutani_msg_window_mouse_event * evt) {
	if (!netstat) {
		netstat = menu_create();
		netstat->flags |= MENU_FLAG_BUBBLE_LEFT;
		menu_insert(netstat, menu_create_normal(NULL, NULL, "<b>Network Status</b>", NULL));
		menu_insert(netstat, menu_create_separator());
	}
	while (netstat->entries->length > 2) {
		node_t * node = list_pop(netstat->entries);
		menu_free_entry((struct MenuEntry *)node->value);
		free(node);
	}
	if (!network_status) {
		menu_insert(netstat, menu_create_normal(NULL, NULL, "No network.", NULL));
	} else {
		for (int i = 0; i < netstat_count; ++i) {
			menu_insert(netstat, menu_create_normal(NULL, NULL, netstat_data[i], NULL));
		}
	}
	if (!netstat->window) {
		panel_menu_show(this,netstat);
	}

	return 1;
}

static int widget_draw_network(struct PanelWidget * this, gfx_context_t * ctx) {
	uint32_t color = (netstat && netstat->window) ? this->pctx->color_text_hilighted : this->pctx->color_icon_normal;
	panel_highlight_widget(this,ctx,(netstat && netstat->window));
	if (network_status & 2) {
		draw_sprite_alpha_paint(ctx, sprite_net_active, (ctx->width - sprite_net_active->width) / 2, 1, 1.0, color);
	} else {
		draw_sprite_alpha_paint(ctx, sprite_net_disabled, (ctx->width - sprite_net_disabled->width) / 2, 1, 1.0, color);
	}

	return 0;
}

struct PanelWidget * widget_init_network(void) {
	sprite_net_active = malloc(sizeof(sprite_t));
	load_sprite(sprite_net_active, "/usr/share/icons/24/net-active.png");
	sprite_net_disabled = malloc(sizeof(sprite_t));
	load_sprite(sprite_net_disabled, "/usr/share/icons/24/net-disconnected.png");

	struct PanelWidget * widget = widget_new();
	widget->width = sprite_net_active->width + widget->pctx->extra_widget_spacing;
	widget->draw = widget_draw_network;
	widget->click = widget_click_network;
	widget->update = widget_update_network;
	list_insert(widgets_enabled, widget);
	return widget;
}

