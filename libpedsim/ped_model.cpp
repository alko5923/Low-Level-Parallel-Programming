//
// pedsim - A microscopic pedestrian simulation system.
// Copyright (c) 2003 - 2014 by Christian Gloor
//
//
// Adapted for Low Level Parallel Programming 2017
//
#include "ped_model.h"
#include "ped_waypoint.h"
#include "ped_model.h"
#include <iostream>
#include <stack>
#include <algorithm>
#include "cuda_testkernel.h"
#include <omp.h>
#include <thread>
#include <emmintrin.h>
#include <smmintrin.h>
#include <stdlib.h>
#include <iostream>
using namespace std;

void Ped::Model::setup(std::vector<Ped::Tagent*> agentsInScenario, std::vector<Twaypoint*> destinationsInScenario, IMPLEMENTATION implementation, int number_of_threads)
{
	// Convenience test: does CUDA work on this machine?
	cuda_test();

	// Set 
	agents = std::vector<Ped::Tagent*>(agentsInScenario.begin(), agentsInScenario.end());

	// Set up destinations
	destinations = std::vector<Ped::Twaypoint*>(destinationsInScenario.begin(), destinationsInScenario.end());

	// Sets the chosen implemenation. Standard in the given code is SEQ
	this->implementation = implementation;

	// Set number of threads to default value
	this->number_of_threads = number_of_threads;

	// Set up heatmap (relevant for Assignment 4)
	setupHeatmapSeq();
	

	if (this->implementation == Ped::SIMD) {
		xArray = (int *) _mm_malloc(agents.size() * sizeof(int), 16);
		yArray = (int *) _mm_malloc(agents.size() * sizeof(int), 16);

		destXarray = (float *) _mm_malloc(agents.size() * sizeof(float), 16);
		destYarray = (float *) _mm_malloc(agents.size() * sizeof(float), 16);
		destRarray = (float *) _mm_malloc(agents.size() * sizeof(float), 16);

		destReached = (int *) _mm_malloc(agents.size() * sizeof(int), 16);

		__m128 t0, t1, t2, t3, t4, t5;

		for (int i = 0; i < agents.size(); i++) {
			xArray[i] =  agents[i]->getX();
			yArray[i] =  agents[i]->getY();

			agents[i]->reallocate_coordinates((int *) &(xArray[i]), (int *) &(yArray[i]));
			
			agents[i]->setDest(agents[i]->getNextDestination());
			// agents[i]->computeNextDesiredPosition();
			
			destXarray[i] = (float) agents[i]->destination->getx();
			destYarray[i] = (float) agents[i]->destination->gety();
			destRarray[i] = (float) agents[i]->destination->getr();

			destReached[i] = 0;
			
		}
	}
}

void thread_func(std::vector<Ped::Tagent*> agents, int start_idx, int end_idx) {
	// The thread function
	// Using a for loop with index

	for(std::size_t i = start_idx; i < end_idx; ++i) {
		agents[i]->computeNextDesiredPosition();
		agents[i]->setX(agents[i]->getDesiredX());
		agents[i]->setY(agents[i]->getDesiredY());
	}
}

void Ped::Model::tick()
{
	// EDIT HERE FOR ASSIGNMENT 1 
	// 1. Retrieve each agent
	// 2. Calculate its next desired position
	// 3. Set its position to the calculated desired one
	//
	if (this->implementation == Ped::SEQ) {
		for (const auto& agent: agents) {
			agent->computeNextDesiredPosition();
			agent->setX(agent->getDesiredX());
			agent->setY(agent->getDesiredY());
		}
	}
	else if (this->implementation == Ped::CTHREADS) {
		std::vector<std::thread> threads;
		int chunk_size = agents.size() / this->number_of_threads;

		for (int i = 0; i < this->number_of_threads; i++) {

			//Make sure not to miss any elements at the end of agent vector
			int end_idx = std::min((i+1)*chunk_size, (int) agents.size());

			threads.push_back(std::thread(thread_func, agents, i*chunk_size, end_idx));
		}

		for (std::thread & t : threads) {
			t.join();
		}
	}
	else if (this->implementation == Ped::OMP) {
		omp_set_num_threads(this->number_of_threads);
		#pragma omp parallel for
		for (const auto& agent: agents) {
			agent->computeNextDesiredPosition();
			agent->setX(agent->getDesiredX());
			agent->setY(agent->getDesiredY());
		}
	}
	else if(this->implementation == Ped::SIMD) {
		__m128 t0, t1, t2, t3, t4, t5, t6, t7, reached, diffX, diffY;
		__m128i xint, yint;
		__m128 xfloat, yfloat;
		
		for (int i = 0; i < agents.size(); i+=4) {
			
			// Load integers and convert to floats for processing
			xint = _mm_load_si128((__m128i*) &xArray[i]);
			t0 = _mm_cvtepi32_ps(xint);
			yint = _mm_load_si128((__m128i*) &yArray[i]);
			t2 = _mm_cvtepi32_ps(yint);

			t1 = _mm_load_ps(&destXarray[i]);
			diffX = _mm_sub_ps(t1, t0); // diffX = destX - agentX
			
			t3 = _mm_load_ps(&destYarray[i]);
			diffY = _mm_sub_ps(t3, t2); // diffY = destY - agentY
			
			// length = sqrt(diffX^2 + diffY^2)
			t4 = _mm_sqrt_ps(_mm_add_ps(_mm_mul_ps(diffX, diffX), _mm_mul_ps(diffY, diffY)));
			t5 = _mm_load_ps(&destRarray[i]);
			reached = _mm_cmpgt_ps(t5, t4);				
		
			// desiredPositionX = (int)round(x + diffX/len);
			// desiredPositionY = (int)round(y + diffY/len);
			// Calculate the desired positions and set them into the x and y arrays
			t6 = _mm_round_ps(_mm_add_ps(t0, _mm_div_ps(diffX, t4)), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
			
			t7 = _mm_round_ps(_mm_add_ps(t2, _mm_div_ps(diffY, t4)), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);

			// Convert and store coordinates
			xint = _mm_cvtps_epi32(t6);
			_mm_store_si128((__m128i*) &xArray[i], xint);
			yint = _mm_cvtps_epi32(t7);
			_mm_store_si128((__m128i*) &yArray[i], yint);

			
			// Set the bit mask and get the indices of set bits in the mask
			int mask = _mm_movemask_ps(reached);
			for (int j = 0; j < 4; j++) {
				int c = mask & 1;

				if (c == 1) {
					Ped::Twaypoint* nextDest = agents[i+j]->getNextDestinationSpecial();
					destXarray[i+j] = nextDest->getx();
					destYarray[i+j] = nextDest->gety();
					destRarray[i+j] = nextDest->getr();
					agents[i+j]->setDest(nextDest);
				}
				mask >>= 1;
			}		
		}	
	}
}

////////////
/// Everything below here relevant for Assignment 3.
/// Don't use this for Assignment 1!
///////////////////////////////////////////////

// Moves the agent to the next desired position. If already taken, it will
// be moved to a location close to it.
void Ped::Model::move(Ped::Tagent *agent)
{
	// Search for neighboring agents
	set<const Ped::Tagent *> neighbors = getNeighbors(agent->getX(), agent->getY(), 2);

	// Retrieve their positions
	std::vector<std::pair<int, int> > takenPositions;
	for (std::set<const Ped::Tagent*>::iterator neighborIt = neighbors.begin(); neighborIt != neighbors.end(); ++neighborIt) {
		std::pair<int, int> position((*neighborIt)->getX(), (*neighborIt)->getY());
		takenPositions.push_back(position);
	}

	// Compute the three alternative positions that would bring the agent
	// closer to his desiredPosition, starting with the desiredPosition itself
	std::vector<std::pair<int, int> > prioritizedAlternatives;
	std::pair<int, int> pDesired(agent->getDesiredX(), agent->getDesiredY());
	prioritizedAlternatives.push_back(pDesired);

	int diffX = pDesired.first - agent->getX();
	int diffY = pDesired.second - agent->getY();
	std::pair<int, int> p1, p2;
	if (diffX == 0 || diffY == 0)
	{
		// Agent wants to walk straight to North, South, West or East
		p1 = std::make_pair(pDesired.first + diffY, pDesired.second + diffX);
		p2 = std::make_pair(pDesired.first - diffY, pDesired.second - diffX);
	}
	else {
		// Agent wants to walk diagonally
		p1 = std::make_pair(pDesired.first, agent->getY());
		p2 = std::make_pair(agent->getX(), pDesired.second);
	}
	prioritizedAlternatives.push_back(p1);
	prioritizedAlternatives.push_back(p2);

	// Find the first empty alternative position
	for (std::vector<pair<int, int> >::iterator it = prioritizedAlternatives.begin(); it != prioritizedAlternatives.end(); ++it) {

		// If the current position is not yet taken by any neighbor
		if (std::find(takenPositions.begin(), takenPositions.end(), *it) == takenPositions.end()) {

			// Set the agent's position 
			agent->setX((*it).first);
			agent->setY((*it).second);

			break;
		}
	}
}

/// Returns the list of neighbors within dist of the point x/y. This
/// can be the position of an agent, but it is not limited to this.
/// \date    2012-01-29
/// \return  The list of neighbors
/// \param   x the x coordinate
/// \param   y the y coordinate
/// \param   dist the distance around x/y that will be searched for agents (search field is a square in the current implementation)
set<const Ped::Tagent*> Ped::Model::getNeighbors(int x, int y, int dist) const {

	// create the output list
	// ( It would be better to include only the agents close by, but this programmer is lazy.)	
	return set<const Ped::Tagent*>(agents.begin(), agents.end());
}

void Ped::Model::cleanup() {
	// Nothing to do here right now. 
}

Ped::Model::~Model()
{
	std::for_each(agents.begin(), agents.end(), [](Ped::Tagent *agent){delete agent;});
	std::for_each(destinations.begin(), destinations.end(), [](Ped::Twaypoint *destination){delete destination; });
}
