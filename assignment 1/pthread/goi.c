#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>
#include "util.h"
#include "exporter.h"
#include "settings.h"
#include <pthread.h>
#include "goi.h"

// including the "dead faction": 0
#define MAX_FACTIONS 10

// this macro is here to make the code slightly more readable, not because it can be safely changed to
// any integer value; changing this to a non-zero value may break the code
#define DEAD_FACTION 0

// struct to contain the args for tasks
typedef struct TaskArgs {
    const int *world;
    const int *inv;
    int *wholeNewWorld;
    int nRows;
    int nCols;
    int startRow;
    int endRow;
    int retVal;
} TaskArgs;

/**
* Specifies the number(s) of live neighbors of the same faction required for a dead cell to become alive.
*/
bool isBirthable(int n)
{
    return n == 3;
}

/**
 * Specifies the number(s) of live neighbors of the same faction required for a live cell to remain alive.
 */
bool isSurvivable(int n)
{
    return n == 2 || n == 3;
}

/**
 * Specifies the number of live neighbors of a different faction required for a live cell to die due to fighting.
 */
bool willFight(int n) {
    return n > 0;
}

void* threadWork(void* args) {
    TaskArgs *tArgs = (TaskArgs*) args;
    int taskDeathToll = 0;
    for (int row = tArgs->startRow; row < tArgs->endRow && row < tArgs->nRows; row++) {
        for (int col = 0; col < tArgs->nCols; col++) {
            bool diedDueToFighting;
            int nextState = getNextState(tArgs->world, tArgs->inv, tArgs->nRows, tArgs->nCols, row, col, &diedDueToFighting);
            setValueAt(tArgs->wholeNewWorld, tArgs->nRows, tArgs->nCols, row, col, nextState);
            if (diedDueToFighting) {
                taskDeathToll++;
            }
        }
    }
    tArgs->retVal = taskDeathToll;
    pthread_exit(0);
}

/**
 * Computes and returns the next state of the cell specified by row and col based on currWorld and invaders. Sets *diedDueToFighting to
 * true if this cell should count towards the death toll due to fighting.
 * 
 * invaders can be NULL if there are no invaders.
 */
int getNextState(const int *currWorld, const int *invaders, int nRows, int nCols, int row, int col, bool *diedDueToFighting)
{
    // we'll explicitly set if it was death due to fighting
    *diedDueToFighting = false;

    // faction of this cell
    int cellFaction = getValueAt(currWorld, nRows, nCols, row, col);

    // did someone just get landed on?
    if (invaders != NULL && getValueAt(invaders, nRows, nCols, row, col) != DEAD_FACTION)
    {
        *diedDueToFighting = cellFaction != DEAD_FACTION;
        return getValueAt(invaders, nRows, nCols, row, col);
    }

    // tracks count of each faction adjacent to this cell
    int neighborCounts[MAX_FACTIONS];
    memset(neighborCounts, 0, MAX_FACTIONS * sizeof(int));

    // count neighbors (and self)
    for (int dy = -1; dy <= 1; dy++)
    {
        for (int dx = -1; dx <= 1; dx++)
        {
            int faction = getValueAt(currWorld, nRows, nCols, row + dy, col + dx);
            if (faction >= DEAD_FACTION)
            {
                neighborCounts[faction]++;
            }
        }
    }

    // we counted this cell as its "neighbor"; adjust for this
    neighborCounts[cellFaction]--;

    if (cellFaction == DEAD_FACTION)
    {
        // this is a dead cell; we need to see if a birth is possible:
        // need exactly 3 of a single faction; we don't care about other factions

        // by default, no birth
        int newFaction = DEAD_FACTION;

        // start at 1 because we ignore dead neighbors
        for (int faction = DEAD_FACTION + 1; faction < MAX_FACTIONS; faction++)
        {
            int count = neighborCounts[faction];
            if (isBirthable(count))
            {
                newFaction = faction;
            }
        }

        return newFaction;
    }
    else
    {
        /** 
         * this is a live cell; we follow the usual rules:
         * Death (fighting): > 0 hostile neighbor
         * Death (underpopulation): < 2 friendly neighbors and 0 hostile neighbors
         * Death (overpopulation): > 3 friendly neighbors and 0 hostile neighbors
         * Survival: 2 or 3 friendly neighbors and 0 hostile neighbors
         */

        int hostileCount = 0;
        for (int faction = DEAD_FACTION + 1; faction < MAX_FACTIONS; faction++)
        {
            if (faction == cellFaction)
            {
                continue;
            }
            hostileCount += neighborCounts[faction];
        }

        if (willFight(hostileCount))
        {
            *diedDueToFighting = true;
            return DEAD_FACTION;
        }

        int friendlyCount = neighborCounts[cellFaction];
        if (!isSurvivable(friendlyCount))
        {
            return DEAD_FACTION;
        }

        return cellFaction;
    }
}

/**
 * The main simulation logic.
 * 
 * goi does not own startWorld, invasionTimes or invasionPlans and should not modify or attempt to free them.
 * nThreads is the number of threads to simulate with. It is ignored by the sequential implementation.
 */
int goi(int nThreads, int nGenerations, const int *startWorld, int nRows, int nCols, int nInvasions, const int *invasionTimes, int **invasionPlans)
{
    // death toll due to fighting
    int deathToll = 0;
    
    // Array to store threads
    pthread_t threads[nThreads];
    
    // Args for each thread
    TaskArgs *tArgs[nThreads];

    /*** Initialise the thread args ***/
    // Number of rows each thread works on
    int rowsPerThread = nRows / nThreads;

    // When nRows % nThreads != 0, we want to
    // split the remainder rows equally amoung
    // each row (i.e. every thread should take on at
    // most 1 more row than rowsPerThread)
    int leftoverRows = nRows % nThreads;

    int startRow = 0, endRow;
    for (int threadIdx = 0; threadIdx < nThreads; threadIdx++)
    {
	  tArgs[threadIdx] = (TaskArgs*) malloc(sizeof(TaskArgs));
	  tArgs[threadIdx]->startRow = startRow;
	  endRow = startRow + rowsPerThread;
	  if (leftoverRows > 0)
	  {
		  leftoverRows--;
		  endRow++;
	  }
	  tArgs[threadIdx]->endRow = endRow;
	  tArgs[threadIdx]->nRows = nRows;
	  tArgs[threadIdx]->nCols = nCols;
	  tArgs[threadIdx]->retVal = 0;
	  startRow = endRow;
    }


    // init the world!
    // we make a copy because we do not own startWorld (and will perform free() on world)
    int *world = malloc(sizeof(int) * nRows * nCols);
    if (world == NULL)
    {
        return -1;
    }
    /*for (int row = 0; row < nRows; row++)
    {
        for (int col = 0; col < nCols; col++)
        {
            setValueAt(world, nRows, nCols, row, col, getValueAt(startWorld, nRows, nCols, row, col));
        }
    }*/
    memcpy(world, startWorld, nRows * nCols * sizeof(int));

#if PRINT_GENERATIONS
    printf("\n=== WORLD 0 ===\n");
    printWorld(world, nRows, nCols);
#endif

#if EXPORT_GENERATIONS
    exportWorld(world, nRows, nCols);
#endif

    // Begin simulating
    int invasionIndex = 0;
    for (int i = 1; i <= nGenerations; i++)
    {
        // is there an invasion this generation?
        int *inv = NULL;
        if (invasionIndex < nInvasions && i == invasionTimes[invasionIndex])
        {
            // we make a copy because we do not own invasionPlans
            inv = malloc(sizeof(int) * nRows * nCols);
            if (inv == NULL)
            {
                free(world);
                return -1;
            }
            /*for (int row = 0; row < nRows; row++)
            {
                for (int col = 0; col < nCols; col++)
                {
                    setValueAt(inv, nRows, nCols, row, col, getValueAt(invasionPlans[invasionIndex], nRows, nCols, row, col));
                }
            }*/
            memcpy(inv, invasionPlans[invasionIndex], nRows * nCols * sizeof(int));
            invasionIndex++;
        }

        // create the next world state
        int *wholeNewWorld = malloc(sizeof(int) * nRows * nCols);
        if (wholeNewWorld == NULL)
        {
            if (inv != NULL)
            {
                free(inv);
            }
            free(world);
            return -1;
        }

        for (int threadIdx = 0; threadIdx < nThreads; threadIdx++) {
            // Set args for each thread
            tArgs[threadIdx]->world = world;
            tArgs[threadIdx]->wholeNewWorld = wholeNewWorld;
            tArgs[threadIdx]->inv = inv;

            int rc = pthread_create(&threads[threadIdx], NULL, threadWork, tArgs[threadIdx]);
            if (rc) {
                printf("Error creating thread\n");
                if (inv != NULL) 
                {
                    free(inv);
                }
                free(world);
                free(wholeNewWorld);
                exit(1);
            } 
        }

        // Join threads
        for (int threadIdx = 0; threadIdx < nThreads; threadIdx++) {
            pthread_join(threads[threadIdx], NULL);
            deathToll += tArgs[threadIdx]->retVal;
	        tArgs[threadIdx]->retVal = 0;
        }


        if (inv != NULL)
        {
            free(inv);
        }

        // swap worlds
        free(world);
        world = wholeNewWorld;

#if PRINT_GENERATIONS
        printf("\n=== WORLD %d ===\n", i);
        printWorld(world, nRows, nCols);
#endif

#if EXPORT_GENERATIONS
        exportWorld(world, nRows, nCols);
#endif
    }

    for (int threadIdx = 0; threadIdx < nThreads; threadIdx++)
    {
	    free(tArgs[threadIdx]);
	    tArgs[threadIdx] = NULL;
    }
    free(world);
    return deathToll;
}
