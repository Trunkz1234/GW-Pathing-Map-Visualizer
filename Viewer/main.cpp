#include <stdio.h>
#include <vector>

#include "GW Path visualizer.h"


int SDL_main(int argc, char** argv) {

	Viewer viewer = Viewer();
	viewer.LoadMapData(argc == 2 ? argv[1] : "mapinfo.csv");
	viewer.InitializeWindow();
	viewer.Execute();
	viewer.Close();

	return 0;
}