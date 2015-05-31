/**
 * Interval timer action to update the date and time in the clock.
 */
function updateTime() {
	var now = moment();
	$("div.clock>.date").html(now.format("dddd") + "<br><b>" + now.format("MMM DD") + "</b>");
	$("div.clock>.time").html(now.format("HH:MM:ss"));
}

/**
 * Turns the clock on and starts the interval timer to update it.
 */
function enableClock() {
	$(".clock").css("visibility", "visible");
	updateTime();
	window.setInterval(updateTime, 1000);
}

/**
 * Bind the close buttons on windows to actually close the windows.
 *
 * This is a bit of an easter egg and provides no useful functionality.
 */
function enableCloseButtons() {
	$(".window>.title>.close").click(function () {
		var parent_window = $(this).parents(".window");
		parent_window.addClass("closed-window");
	});
}

function lightboxUnzoom() {
	var h3 = $(this).parent();
	h3.animate({
		width: 400
	},300);
	$(this).animate({
		width: 404
	},300);
	$(this).off("click");
	$(this).click(lightboxZoom);
	return false;
}

function lightboxZoom() {
	var h3 = $(this).parent();
	var title = $("em", h3).text();
	var img = $(this).attr('href');
	var lightboxWindow = $('<div class="lightbox closed-window"><div class="center"><div class="window-pad"><div class="window"><div class="title">' + title + '<div class="close"></div></div><div class="content"><img src="' + img + '" /></div></div></div></div></div>');

	$('html').append(lightboxWindow);

	setTimeout(function() {
		lightboxWindow.removeClass("closed-window");
	}, 10);

	$("img", lightboxWindow).css('max-width', $(window).width() - 100);
	$("img", lightboxWindow).css('max-height', $(window).height() - 100);

	var top_pad = $("#menu").height();

	lightboxWindow.css('left', ($(window).width() - lightboxWindow.width()) / 2);
	lightboxWindow.css('top', top_pad + ($(window).height() - top_pad - lightboxWindow.height()) / 2);

	$(".window>.title>.close", lightboxWindow).click(function () {
		var parent_window = $(this).parents(".window");
		parent_window.addClass("closed-window");
		setTimeout(function() {
			parent_window.hide();
		}, 450);
	});
	$(".window img", lightboxWindow).click(function () {
		var parent_window = $(this).parents(".window");
		parent_window.addClass("closed-window");
		setTimeout(function() {
			parent_window.hide();
		}, 450);
	});

	return false;
}

function enableLightbox() {
	$("h3>a").click(lightboxZoom);
}

$( document ).ready(function() {
	enableClock();
	enableCloseButtons();
	enableLightbox();
});
