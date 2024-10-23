#pragma once

#include <vector>

#include <SDL.h>
#include <SDL_opengl.h>

#include "Point2d.h"

#include "PathingMap.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

struct GWMapData {
	unsigned mapid;
	char name[0x100];
	unsigned mapfile;
	float spawnx;
	float spawny;
	bool selected;
	bool visible;
};

enum MapTool {
	kToolWaypointer,
	kToolRuler,

	kToolEnd
};

class Viewer {
private:
	static const int DEFAULT_WIDTH = 800;
	static const int DEFAULT_HEIGHT = 500;
	size_t selectedWaypointIndex = SIZE_MAX;

	int width_;
	int height_;

	SDL_Window* window;
	std::vector<PathingMapTrapezoid> trapezoids_;
	int max_plane_;

	bool mouse_down_;
	bool right_mouse_down_;

	bool refresh_;
	bool wireframe_;
	bool circles_;
	Point2d center_;
	std::vector<Point2d> circle_vertices_;
	std::vector<int> circle_sizes_;

	double scale_;
	Point2d translate_;
	double ratio_;
	GWMapData* mapdata_;
	GWMapData* currentmap_;
	unsigned mapdatacount_;

	MapTool currTool_;

	std::vector<Point2d> waypoints_;
	double waypointDistance_;
	Point2d rulerPoints_[2];


public:
	Viewer();

	void InitializeWindow();

	void LoadMapData(const char* file);

	void Resize(int width, int height);

	void Execute();

	void SetPMap(unsigned mapfileid);

	void RenderPMap();

	void Close();

	void ScreenToWorld(Point2d& out, Point2d& in);

	void HandleMouseDownEvent(SDL_MouseButtonEvent);
	void HandleMouseUpEvent(SDL_MouseButtonEvent);
	void HandleMouseMoveEvent(SDL_MouseMotionEvent);
	void HandleMouseWheelEvent(SDL_MouseWheelEvent);
	void HandleKeyDownEvent(SDL_KeyboardEvent);
	void HandleKeyUpEvent(SDL_KeyboardEvent);

	void HandleWindowEvent(SDL_WindowEvent);
};