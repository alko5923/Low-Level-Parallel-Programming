///////////////////////////////////////////////////
// Low Level Parallel Programming 2017.
//
// 
//
// The main starting point for the crowd simulation.
//



#undef max
#include "ped_model.h"
#include "MainWindow.h"
#include "ParseScenario.h"

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QApplication>
#include <QTimer>
#include <thread>

#include "PedSimulation.h"
#include <iostream>
#include <chrono>
#include <ctime>
#include <cstring>

#pragma comment(lib, "libpedsim.lib")

#include <stdlib.h>

int main(int argc, char*argv[]) {
	bool timing_mode = 0;
	int i = 1;
	QString scenefile = "scenario.xml";

	// Change this variable when testing different versions of your code. 
	// May need modification or extension in later assignments depending on your implementations
	Ped::IMPLEMENTATION implementation_to_test = Ped::SEQ;

	// Number of threads to use in PTHREADS implementation
	int number_of_threads = 2;

	// Argument handling
	while (i < argc)
	{
		if (argv[i][0] == '-' && argv[i][1] == '-')
		{
			if (strcmp(&argv[i][2], "timing-mode") == 0)
			{
				cout << "Timing mode on\n";
				timing_mode = true;
			}
			else if (strcmp(&argv[i][2], "help") == 0)
			{
				cout << "Usage: " << argv[0] << " [--help] [--timing-mode] [--implementation IMPL] [--threads N] [scenario]" << endl;
				return 0;
			}
			else if (strcmp(&argv[i][2], "implementation") == 0)
			{
				i += 1;
				if (strcmp(&argv[i][0], "CTHREADS") == 0)
				{ 
					implementation_to_test = Ped::CTHREADS;
				}
				else if (strcmp(&argv[i][0], "OMP") == 0) 
                                {
                                        implementation_to_test = Ped::OMP;
                                }
				else if (strcmp(&argv[i][0], "SIMD") == 0) 
                                {
                                        implementation_to_test = Ped::SIMD;
                                }
				else if (strcmp(&argv[i][0], "CUDA") == 0) 
                                {
                                        implementation_to_test = Ped::CUDA;
                                }
				else if (strcmp(&argv[i][0], "SEQ") != 0)
				{
					cerr << "Unrecognized implementation: \"" << argv[i] << "\". Try one of SEQ | PTHREADS  " << endl;
				}
			}
			else if (strcmp(&argv[i][2], "threads") == 0)
			{
				i += 1;
				number_of_threads = std::stoi(&argv[i][0]);
			}
			else
			{
				cerr << "Unrecognized command: \"" << argv[i] << "\". Ignoring ..." << endl;
			}
		}
		else // Assume it is a path to scenefile
		{
			scenefile = argv[i];
		}

		i += 1;
	}
	int retval = 0;
	{ // This scope is for the purpose of removing false memory leak positives

		// Reading the scenario file and setting up the crowd simulation model
		Ped::Model model;
		ParseScenario parser(scenefile);
		model.setup(parser.getAgents(), parser.getWaypoints(), implementation_to_test);

		// Default number of steps to simulate. Feel free to change this.
		const int maxNumberOfStepsToSimulate = 1000;
		
				

		// Timing version
		// Run twice, without the gui, to compare the runtimes.
		// Compile with timing-release to enable this automatically.
		if (timing_mode)
		{
            //MainWindow mainwindow(model, timing_mode);
			// Run sequentially

			double fps_seq, fps_target;
			{
				Ped::Model model;
				ParseScenario parser(scenefile);
				model.setup(parser.getAgents(), parser.getWaypoints(), Ped::SEQ);
				PedSimulation simulation(model, NULL, timing_mode);
				// Simulation mode to use when profiling (without any GUI)
				std::cout << "Running reference version...\n";
				auto start = std::chrono::steady_clock::now();
				simulation.runSimulation(maxNumberOfStepsToSimulate);
				auto duration_seq = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now() - start);
				fps_seq = ((float)simulation.getTickCount()) / ((float)duration_seq.count())*1000.0;
				cout << "Reference time: " << duration_seq.count() << " milliseconds, " << fps_seq << " Frames Per Second." << std::endl;
			}

			{
				Ped::Model model;
				ParseScenario parser(scenefile);
				model.setup(parser.getAgents(), parser.getWaypoints(), implementation_to_test, number_of_threads);
				PedSimulation simulation(model, NULL, timing_mode);
				// Simulation mode to use when profiling (without any GUI)
				std::cout << "Running target version...\n";
				auto start = std::chrono::steady_clock::now();
				simulation.runSimulation(maxNumberOfStepsToSimulate);
				auto duration_target = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now() - start);
				fps_target = ((float)simulation.getTickCount()) / ((float)duration_target.count())*1000.0;
				cout << "Target time: " << duration_target.count() << " milliseconds, " << fps_target << " Frames Per Second." << std::endl;
			}
			std::cout << "\n\nSpeedup: " << fps_target / fps_seq << std::endl;
			
			

		}
		// Graphics version
		else
		{
            QApplication app(argc, argv);
            MainWindow mainwindow(model);

			PedSimulation simulation(model, &mainwindow, timing_mode);

			cout << "Demo setup complete, running ..." << endl;

			// Simulation mode to use when visualizing
			auto start = std::chrono::steady_clock::now();
			mainwindow.show();
			simulation.runSimulation(maxNumberOfStepsToSimulate);
			retval = app.exec();

			auto duration = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now() - start);
			float fps = ((float)simulation.getTickCount()) / ((float)duration.count())*1000.0;
			cout << "Time: " << duration.count() << " milliseconds, " << fps << " Frames Per Second." << std::endl;
			
		}

		

		
	}

	cout << "Done" << endl;
	return retval;
}
