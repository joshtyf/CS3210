#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include "util.h"
#include "exporter.h"
#include "settings.h"
#include "pthread_pool.h"
#include "goi.h"

// including the "dead faction": 0
#define MAX_FACTIONS 10

// this macro is here to make the code slightly more readable, not because it can be safely changed to
// any integer value; changing this to a non-zero value may break the code
#define DEAD_FACTION 0

#define TASK_SIZE 3

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

typedef struct taskArgs {
    const int* world;
    const int* inv;
    int *wholeNewWorld;
    int nRows;
    int nCols;
    int row;
    int *deathToll;
    pthread_mutex_t *lock;
} TaskArgs;

void* threadTask(void* args) {
    TaskArgs *tArgs = (TaskArgs *) args;
    for (int i = 0; i < TASK_SIZE; i++) {
	    // each task operates on TASK_SIZE rows
	    if (tArgs->row >= tArgs->nRows) {
		    break;
	    }
	    for (int col = 0; col < tArgs->nCols; col++)
	    {
		bool diedDueToFighting;
		int nextState = getNextState(tArgs->world, tArgs->inv, tArgs->nRows, tArgs->nCols, tArgs->row, col, &diedDueToFighting);
		setValueAt(tArgs->wholeNewWorld, tArgs->nRows, tArgs->nCols, tArgs->row, col, nextState);
		if (diedDueToFighting)
		{
		    pthread_mutex_lock(tArgs->lock);
		    (*(tArgs->deathToll))++;
		    pthread_mutex_unlock(tArgs->lock);
		}
	    }
	    (tArgs->row)++;
    }
   return NULL;
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

    // init the world!
    // we make a copy because we do not own startWorld (and will perform free() on world)
    int *world = malloc(sizeof(int) * nRows * nCols);
    if (world == NULL)
    {
	printf("Failed to mem alloc for world\n");
        return -1;
    }
    memcpy(world, startWorld, nRows * nCols * sizeof(int));

    // init thread pool
    struct pool *p = (struct pool *)pool_start(threadTask, nThreads);

    // init mutex
    pthread_mutex_t lock;
    if (pthread_mutex_init(&lock, NULL) != 0) {
        printf("Failed to initialise mutex\n");
        pool_end(p);
        free(world);
        exit(1);
    }


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
        //printf("gen %d\n", i);
        // is there an invasion this generation?
        int *inv = NULL;
        if (invasionIndex < nInvasions && i == invasionTimes[invasionIndex])
        {
            // we make a copy because we do not own invasionPlans
            inv = malloc(sizeof(int) * nRows * nCols);
            if (inv == NULL)
            {
		printf("Failed to mem alloc for inv\n");
                free(world);
                return -1;
            }
            memcpy(inv, invasionPlans[invasionIndex], nRows * nCols * sizeof(int));
            invasionIndex++;
        }

        // create the next world state
        int *wholeNewWorld = malloc(sizeof(int) * nRows * nCols);
        if (wholeNewWorld == NULL)
        {
	    printf("Failed to mem alloc for wholeNewWorld\n");
            if (inv != NULL)
            {
                free(inv);
            }
            free(world);
            return -1;
        }

        // get new states for each cell
        for (int row = 0; row < nRows; row += TASK_SIZE)
        {
	    // each task operates on 3 rows
            TaskArgs *tArgs = (TaskArgs *)malloc(sizeof(TaskArgs));
            if (tArgs == NULL) {
                printf("Failed to mem alloc for task args\n");
                pool_end(p);
        		free(world);
        		free(wholeNewWorld);
        		if (inv != NULL) {
        		    free(inv);
        		}
                        exit(1);
            }

            tArgs->world = world;
            tArgs->wholeNewWorld = wholeNewWorld;
            tArgs->inv = inv;
            tArgs->nRows = nRows;
            tArgs->nCols = nCols;
            tArgs->row = row;
            tArgs->deathToll = &deathToll;
            tArgs->lock = &lock;
            pool_enqueue(p, tArgs, 1);
        }
        pool_wait(p);

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
    pool_wait(p);
    pool_end(p);

    free(world);
    return deathToll;
}
