
#include "window_manager.h"

#include "renderer.h"

os::WindowRef gui::window_manager::create_window(os::DragTarget& drag_target) {
	auto screen = os::instance()->mainScreen();

	os::WindowRef window = os::instance()->makeWindow(591, 381);
	window->setCursor(os::NativeCursor::Arrow);
	window->setTitle("Blur");
	window->setDragTarget(&drag_target);

	return window;
}

void gui::window_manager::on_resize(os::Window* window) {
	renderer::redraw_window(window, true);
}
