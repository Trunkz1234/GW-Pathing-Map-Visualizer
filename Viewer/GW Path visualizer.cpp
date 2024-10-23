#include "GW Path visualizer.h"
#include <cmath>
#include <string>

void DebugWait()
{
#ifdef _DEBUG
	while (!IsDebuggerPresent())
	{
		Sleep(100);
	}
#endif
}

Viewer::Viewer()
{
	window = nullptr;
	trapezoids_ = std::vector<PathingMapTrapezoid>();
	max_plane_ = 1;
	mouse_down_ = false;
	right_mouse_down_ = false;
	refresh_ = false;
	scale_ = 0.0001;
	translate_ = Point2d();
	wireframe_ = false;
	circles_ = false;
	mapdata_ = nullptr;
	mapdatacount_ = 0;
	currentmap_ = nullptr;
	waypointDistance_ = 0;

	int n_vertices = 50;
	for (int i = 0; i < 50; ++i) {
		float angle = i * (2 * static_cast<float>(M_PI) / n_vertices);
		float x = std::cos(angle);
		float y = std::sin(angle);
		circle_vertices_.push_back(Point2d(x, y));
	}
	circle_vertices_.push_back(circle_vertices_[0]);

	circle_sizes_.push_back(300); // nearby aka hos
	circle_sizes_.push_back(322);
	circle_sizes_.push_back(366);
	circle_sizes_.push_back(1085); // spellcast
	circle_sizes_.push_back(2500); // spirit range
	circle_sizes_.push_back(5000); // compass
}

void Viewer::InitializeWindow() {
	SDL_Init(SDL_INIT_VIDEO);
	window = SDL_CreateWindow("Pathing visualization",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		DEFAULT_WIDTH, DEFAULT_HEIGHT,
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED);

	SDL_GL_CreateContext(window);
	SDL_GL_SetSwapInterval(1);

	glDisable(GL_LIGHTING);
	glDisable(GL_DEPTH_TEST);

	// Initialize ImGui
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io; // Setup ImGui context
	ImGui_ImplSDL2_InitForOpenGL(window, SDL_GL_GetCurrentContext());
	ImGui_ImplOpenGL3_Init("#version 130"); // Adjust the version as necessary

	Resize(DEFAULT_WIDTH, DEFAULT_HEIGHT);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

void Viewer::Resize(int width, int height) {
	width_ = width;
	height_ = height;
	printf("resizing to %d %d\n", width, height);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	ratio_ = (double)width / height;
	glScaled(1, ratio_, 1);

	refresh_ = true;
}

void Viewer::LoadMapData(const char* file)
{
	//DebugWait();

	FILE* fh = fopen("mapinfo.csv", "r");
	if (!fh) {
		printf("Mapdata.csv fopen error!\n");
		return;
	}

	// Figure out how many map tables we need
	mapdatacount_ = 0;
	int c;
	do {
		c = fgetc(fh);
		if (c == '\n')
			mapdatacount_++;
	} while (c != EOF);
	mapdata_ = new GWMapData[mapdatacount_];
	fseek(fh, 0, SEEK_SET);

	unsigned idx = 0;
	char buffer[100];
	for (fgets(buffer, 100, fh); !feof(fh); fgets(buffer, 100, fh), ++idx) {
		char* seeker = buffer;

		// get mapid
		mapdata_[idx].mapid = strtoul(seeker, nullptr, 0);
		seeker = strchr(seeker, ',');
		seeker++;

		// get map name
		{
			unsigned namecount = 0;
			for (; *seeker != ',' && namecount < 0x100 - 1; seeker++, namecount++) {
				mapdata_[idx].name[namecount] = *seeker;
			}
			mapdata_[idx].name[namecount] = '\0';
			seeker++;
		}

		// get mapfile
		mapdata_[idx].mapfile = strtoul(seeker, nullptr, 0);
		seeker = strchr(seeker, ',');
		seeker++;

		// get spawn x
		mapdata_[idx].spawnx = atof(seeker);
		seeker = strchr(seeker, ',');
		seeker++;

		// get spawn y
		mapdata_[idx].spawny = atof(seeker);
		seeker = strchr(seeker, ',');
		seeker++;

		mapdata_[idx].selected = false;
		mapdata_[idx].visible = true;
	}


}

DWORD WINAPI PmapExtractor(LPVOID ab)
{
	LPSTR argbuffer = (LPSTR)ab;
	STARTUPINFOA pmapBuildInfo = {};
	PROCESS_INFORMATION pmapPI = {};
	if (!CreateProcessA(NULL, argbuffer, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &pmapBuildInfo, &pmapPI))
		printf("error in create process, err %d\n", GetLastError());
	WaitForSingleObject(pmapPI.hProcess, INFINITE);
	return 0;
}

void Viewer::Execute() {
	bool quit = false;
	while (!quit) {
		SDL_Delay(1);
		// event handling
		SDL_Event e;
		while (SDL_PollEvent(&e) != 0) {
			ImGui_ImplSDL2_ProcessEvent(&e); // Process ImGui events

			switch (e.type) {
			case SDL_QUIT:
				quit = true;
				break;
			case SDL_MOUSEBUTTONDOWN:
				HandleMouseDownEvent(e.button);
				break;
			case SDL_MOUSEBUTTONUP:
				HandleMouseUpEvent(e.button);
				break;
			case SDL_MOUSEMOTION:
				HandleMouseMoveEvent(e.motion);
				break;
			case SDL_MOUSEWHEEL:
				HandleMouseWheelEvent(e.wheel);
				break;
			case SDL_WINDOWEVENT:
				HandleWindowEvent(e.window);
				break;
			case SDL_KEYDOWN:
				HandleKeyDownEvent(e.key);
				break;
			case SDL_KEYUP:
				HandleKeyUpEvent(e.key);
				break;
			default:
				break;
			}
		}

		// Start the ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();

		//if (refresh_) {
			RenderPMap();
			refresh_ = false;
		//}


		ImGuiViewport* viewport = ImGui::GetMainViewport();

		// PMAP downloader
		static bool   download_prompt = GetFileAttributesA("PMAPs") == INVALID_FILE_ATTRIBUTES;
		static HANDLE download_thread = 0;
		if (download_prompt || download_thread) {
			if (download_thread) {
				if (WaitForSingleObject(download_thread, 0) == WAIT_OBJECT_0) {
					download_thread = 0;
				}
				else {
					ImGui::Begin("waitForDownload", nullptr, ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoTitleBar);
					ImGui::Text("Please wait for extraction...");
					ImGui::End();
					goto endRender;
				}
			}

			if (ImGui::BeginPopupModal("pmapDownloadPrompt", &download_prompt, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar)) {
				static char argbuffer[MAX_PATH + 20] = "pmap.exe -e \"";
				static char* writer = argbuffer + strlen(argbuffer);
				ImGui::Text("PMAP directory not found,\nplease input your datfile and hit run.");
				ImGui::InputText("datfile path", writer, MAX_PATH);
				if (ImGui::Button("Go")) {
					strcat(argbuffer, "\"");
					download_thread = CreateThread(0, 0, PmapExtractor, argbuffer, 0, 0);
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			static bool pmap_download = download_prompt;
			if (pmap_download) {
				download_prompt = true;
				ImGui::OpenPopup("pmapDownloadPrompt");
				pmap_download = false;
			}
		}
		else {
			ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y), ImGuiCond_Always);
			ImGui::SetNextWindowSize(ImVec2(360, viewport->Size.y * 0.25f), ImGuiCond_Always);
			if (ImGui::Begin("Info Window", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGuiIO& io = ImGui::GetIO(); // Get ImGui IO
				Point2d lel(io.MousePos.x, io.MousePos.y), lel2;

				ScreenToWorld(lel2, lel);
				ImGui::LabelText("Center", "(%f,%f)", center_.x(), center_.y());
				ImGui::LabelText("Translate", "(%f,%f)", translate_.x(), translate_.y());
				ImGui::LabelText("lel", "(%f,%f)", lel2.x(), lel2.y());
				ImGui::LabelText("W/H", "(%d,%d)", width_, height_);
				ImGui::LabelText("Scale", "%f", scale_);
				ImGui::LabelText("Ratio", "%f", ratio_);
				ImGui::LabelText("Distance", "%f", waypointDistance_);
			}
			ImGui::End();

			// Set window position to bottom left
			ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - viewport->Size.y * 0.75f), ImGuiCond_Always);
			ImGui::SetNextWindowSizeConstraints(ImVec2(360, 100), ImVec2(360, 4000));
			if (ImGui::Begin("Map List", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::Columns(4, NULL, false);

				ImGui::SetColumnWidth(-1, 1);
				ImGui::Text("");
				for (unsigned i = 0; i < mapdatacount_; ++i) {
					ImGui::PushID(i);
					if (ImGui::Selectable("##Load", &mapdata_[i].selected, ImGuiSelectableFlags_SpanAllColumns)) {
						if (currentmap_)
							currentmap_->selected = false;
						currentmap_ = &mapdata_[i];
						SetPMap(mapdata_[i].mapfile);
					}
					ImGui::PopID();
				}

				ImGui::NextColumn();
				ImGui::SetColumnWidth(-1, 40);
				ImGui::Text("ID");
				for (unsigned i = 0; i < mapdatacount_; ++i) {
					ImGui::Text("%d", mapdata_[i].mapid);
				}

				ImGui::NextColumn();
				ImGui::SetColumnWidth(-1, 200);
				ImGui::Text("Name");
				for (unsigned i = 0; i < mapdatacount_; ++i) {
					ImGui::Text(mapdata_[i].name);
				}

				ImGui::NextColumn();
				ImGui::SetColumnWidth(-1, 80);
				ImGui::Text("Map File");
				for (unsigned i = 0; i < mapdatacount_; ++i) {
					ImGui::Text("%d", mapdata_[i].mapfile);
				}
			}
			ImGui::End();

			ImGui::SetNextWindowPos(ImVec2(360, 0), ImGuiCond_Always);  // Position after Info Window
			ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x - (360 * 2), 60), ImGuiCond_Always);
			if (ImGui::Begin("Toolbox", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
				if (ImGui::Button("Waypointer")) {
					currTool_ = kToolWaypointer;
				}
				ImGui::SameLine();
				if (ImGui::Button("Ruler")) {
					currTool_ = kToolRuler;
				}
			}
			ImGui::End();

			if (currTool_ == kToolWaypointer) {
				ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 360, 0));  // Position at top-right corner
				ImGui::SetNextWindowSize(ImVec2(360, ImGui::GetIO().DisplaySize.y));  // Full screen height

				if (ImGui::Begin("Waypoints")) {
					if (ImGui::Button("Clear##Waypoints")) {
						waypointDistance_ = 0;
						waypoints_.clear();
					}
					ImGui::SameLine();
					if (ImGui::Button("Send to Clipboard##Waypoints")) {
						ImGuiTextBuffer buffer;
						for (size_t i = 0; i < waypoints_.size(); i++) {
							auto& p = waypoints_[i];
							buffer.appendf("%06.2f, %06.2f\n", p.x(), p.y());
						}
						SDL_SetClipboardText(buffer.c_str());
					}
					ImGui::Separator();

					// Define the table with four columns: Index, X, Y, and Action
					if (ImGui::BeginTable("WaypointsTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY, ImVec2(0, 0))) {
						// Set up table headers
						ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 50.0f);
						ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthFixed, 100.0f);
						ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthFixed, 100.0f);
						ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 100.0f);
						ImGui::TableHeadersRow();

						// Iterate over waypoints and populate rows
						for (size_t i = 0; i < waypoints_.size(); ++i) {
							ImGui::TableNextRow();  // Move to the next row

							// Index column (Selectable)
							ImGui::TableSetColumnIndex(0);
							if (ImGui::Selectable(std::to_string(i).c_str(), selectedWaypointIndex == i)) {
								selectedWaypointIndex = i;
							}

							// X coordinate column
							ImGui::TableSetColumnIndex(1);
							ImGui::Text("%s", std::to_string(waypoints_[i].y()).c_str());

							// Y coordinate column
							ImGui::TableSetColumnIndex(2);
							ImGui::Text("%s", std::to_string(waypoints_[i].y()).c_str());

							// Action column (e.g., a button to delete a waypoint)
							ImGui::TableSetColumnIndex(3);
							if (ImGui::Button(("Delete##" + std::to_string(i)).c_str())) {
								// Remove the waypoint when the button is clicked
								waypoints_.erase(waypoints_.begin() + i);
								// If the selected waypoint was deleted, reset the selection
								if (selectedWaypointIndex == i) {
									selectedWaypointIndex = SIZE_MAX; // Reset to no selection
								}
								else if (selectedWaypointIndex > i) {
									selectedWaypointIndex--; // Adjust index if necessary
								}
							}
						}

						ImGui::EndTable();
					}
				}
				ImGui::End();
			}
			else if (currTool_ == kToolRuler) {
				if (ImGui::Begin("Ruler")) {
					ImGui::LabelText("Pos1", "(%f,%f)", rulerPoints_[0].x(), rulerPoints_[0].y());
					ImGui::LabelText("Pos2", "(%f,%f)", rulerPoints_[1].x(), rulerPoints_[1].y());
					ImGui::LabelText("Distance", "%f", rulerPoints_[0].distanceFrom(rulerPoints_[1]));
				}
				ImGui::End();
			}
		}

	endRender:
		// Render ImGui
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		SDL_GL_SwapWindow(window);
	}
}

void Viewer::SetPMap(unsigned mapfileid) {
	TCHAR filename[MAX_PATH];
	_stprintf_s(filename, TEXT("PMAPs\\MAP %010u.pmap"), mapfileid);
	PathingMap pmap(mapfileid);
	if (!pmap.Open(filename)) {
		printf("ERROR loading pmap %d\n", mapfileid);
	}
	trapezoids_ = pmap.GetPathingData();

	max_plane_ = 1;
	for (size_t i = 0; i < trapezoids_.size(); ++i) {
		if (max_plane_ < trapezoids_[i].Plane) {
			max_plane_ = trapezoids_[i].Plane;
		}
	}
	waypoints_.clear();
	refresh_ = true;
}

void Viewer::RenderPMap() {
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glViewport(0, 0, width_, height_);
	glScaled(scale_, scale_, 1);
	glPushMatrix();
	glTranslated(translate_.x(), translate_.y(), 0);

	if (wireframe_) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); // wireframe
	}
	else {
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); // full quads
	}

	// Render the trapezoids
	for (size_t i = 0; i < trapezoids_.size(); ++i) {
		if (trapezoids_[i].Plane == 0) {
			glColor3f(0.5f, 0.5f, 0.5f); // Grey for plane 0
		}
		else {
			glColor3f(0.5f, 0.0f, 0.5f); // Purple for plane != 0
		}

		glBegin(GL_QUADS);
		glVertex2f(trapezoids_[i].XTL, trapezoids_[i].YT);
		glVertex2f(trapezoids_[i].XTR, trapezoids_[i].YT);
		glVertex2f(trapezoids_[i].XBR, trapezoids_[i].YB);
		glVertex2f(trapezoids_[i].XBL, trapezoids_[i].YB);
		glEnd();
	}

	// Render waypoints
	{
		glColor3f(0, 0, 1); // Default color for waypoints
		glBegin(GL_LINE_STRIP);
		for (size_t i = 0; i < waypoints_.size(); ++i) {
			Point2d& wp = waypoints_[i];
			glVertex2f(wp.x(), wp.y());
		}
		glEnd();

		for (size_t i = 0; i < waypoints_.size(); ++i) {
			Point2d& wp = waypoints_[i];
			glBegin(GL_TRIANGLE_FAN);
			glColor3f((selectedWaypointIndex == i) ? 0 : 1, (selectedWaypointIndex == i) ? 1 : 0, 0); // Green if selected, otherwise white
			for (float j = 0; j <= 6.28318; j += (6.28318 / 60)) {
				glVertex2f(wp.x() + 50 * cos(j), wp.y() + 50 * sin(j));
			}
			glEnd();
		}
	}

	if (circles_) {
		glPopMatrix();
		glTranslated(center_.x(), center_.y(), 0.0);
		glColor3f(1.0f, 1.0f, 1.0f);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); // full quads

		glBegin(GL_POINTS);
		glVertex2f(0.0f, 0.0f);
		glEnd();

		for (size_t j = 0; j < circle_sizes_.size(); ++j) {
			glPushMatrix();
			glScaled(circle_sizes_[j], circle_sizes_[j], 0.0);

			glBegin(GL_LINE_STRIP);
			for (size_t i = 0; i < circle_vertices_.size(); ++i) {
				glVertex2d(circle_vertices_[i].x(), circle_vertices_[i].y());
			}
			glEnd();
			glPopMatrix();
		}

		// Uncomment this if needed
		// glBegin(GL_TRIANGLES);
		// glVertex2f(center_.x(), center_.y());
		// glVertex2f(center_.x() + 500, center_.y() - 500);
		// glVertex2f(center_.x() - 500, center_.y() - 500);
		// glEnd();
	}

	glPopMatrix();
}


void Viewer::Close() {
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();

	SDL_DestroyWindow(window);
	SDL_Quit();
}

void Viewer::HandleMouseDownEvent(SDL_MouseButtonEvent button) {
	if (button.button == SDL_BUTTON_LEFT) {
		mouse_down_ = true;
	}
	else if (button.button == SDL_BUTTON_RIGHT) {
		rulerPoints_[0] = Point2d(button.x, button.y);
		ScreenToWorld(rulerPoints_[0], rulerPoints_[0]);
		right_mouse_down_ = true;
	}
}

void Viewer::HandleMouseUpEvent(SDL_MouseButtonEvent button) {
	if (button.button == SDL_BUTTON_LEFT) {
		mouse_down_ = false;
	}
	else if (button.button == SDL_BUTTON_RIGHT) {
		right_mouse_down_ = false;
		if (currTool_ == kToolWaypointer) {
			Point2d pos = Point2d(button.x, button.y);
			ScreenToWorld(pos, pos);
			if (waypoints_.size() > 0) {
				Point2d prev_wp_diff = waypoints_[waypoints_.size() - 1] - pos;
				waypointDistance_ += sqrt((prev_wp_diff.x() * prev_wp_diff.x()) + (prev_wp_diff.y() * prev_wp_diff.y()));
			}
			waypoints_.push_back(pos);
		}
		else if (currTool_ == kToolRuler) {

		}
	}
}

void Viewer::HandleMouseMoveEvent(SDL_MouseMotionEvent motion) {
	if (mouse_down_) {
		Point2d diff = Point2d(motion.xrel, -motion.yrel);
		diff.x() /= width_; // remap from [0, WIDTH] to [0, 1]
		diff.y() /= height_; // remap from [0, HEIGHT] to [0, 1]
		diff.y() /= ratio_; // adjust for window aspect ratio
		diff *= 2; // remap from [0, 1]^2 to [0, 2]^2 (screen space is [-1, 1] so range has to be 2
		diff /= scale_; // remap for scale

		translate_ += diff;

		refresh_ = true;
	}

	{
		center_ = Point2d(motion.x, motion.y);
		center_.x() /= width_; // remap from [0, Width] to [0, 1]
		center_.y() /= -height_; // remap from [0, Height] to [0, 1]
		center_.y() += 0.5f;
		center_.x() -= 0.5f;
		center_.y() /= ratio_; // adjust for window aspect ratio
		center_ *= 2; // remap from [0, 1]^2 to [0, 2]^2
		center_ /= scale_;

		refresh_ = true;
	}
}

void Viewer::ScreenToWorld(Point2d& out, Point2d& in)
{
	out = in;
	out.x() /= width_; // remap from [0, Width] to [0, 1]
	out.y() /= -height_; // remap from [0, Height] to [0, 1]
	out.y() += 0.5f;
	out.x() -= 0.5f;
	out.y() /= ratio_; // adjust for window aspect ratio
	out *= 2; // remap from [0, 1]^2 to [0, 2]^2
	out /= scale_;
	out -= translate_;
}

void Viewer::HandleMouseWheelEvent(SDL_MouseWheelEvent wheel) {
	if (wheel.y > 0) {
		scale_ *= 1.25;
	}
	else {
		scale_ *= 0.8;
	}
	refresh_ = true;
}

void Viewer::HandleWindowEvent(SDL_WindowEvent window) {
	switch (window.event) {
	case SDL_WINDOWEVENT_RESIZED:
		Resize(window.data1, window.data2);
		break;
	default:
		break;
	}
}

void Viewer::HandleKeyDownEvent(SDL_KeyboardEvent keyboard) {
	switch (keyboard.keysym.sym) {
	default:
		break;
	}
}

void Viewer::HandleKeyUpEvent(SDL_KeyboardEvent keyboard) {
	switch (keyboard.keysym.sym) {
	case SDLK_SPACE:
		wireframe_ = !wireframe_;
		refresh_ = true;
		break;
	case SDLK_c:
		circles_ = !circles_;
		refresh_ = true;
		break;
	default:
		break;
	}
}