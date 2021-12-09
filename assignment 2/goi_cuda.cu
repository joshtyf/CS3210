#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "util.h"

int readParam(FILE *fp, char **line, size_t *len, int *param);
int readWorldLayout(FILE *fp, char **line, size_t *len, int *world, int nRows, int nCols);

int main(int argc, char** argv)
{
    if (argc < 9) {
        fprintf(stderr, "USAGE: ./goi_cuda <input> <output> <grid_x> grid_y> <grid_z> <block_x> <block_y> <block_z>\n");
        exit(1);
    }

    char* INPUT_FILE_PATH = argv[1];
    char* OUTPUT_FILE_PATH = argv[2];
    int GRID_X;
    int GRID_Y;
    int GRID_Z;
    int BLOCK_X;
    int BLOCK_Y;
    int BLOCK_Z;
    int N_ROWS;
    int N_COLS;
    int N_GENERATIONS;
    int *START_WORLD;
    int N_INVASIONS;
    int *INVASION_TIMES;
    int **INVASION_PLANS;
    

    // Open files
    FILE* INPUT_FILE = fopen(INPUT_FILE_PATH, "r");
    if (INPUT_FILE == NULL) {
        fprintf(stderr, "Failed to open %s for reading.\n", INPUT_FILE_PATH);
        exit(1);
    }

    FILE* OUTPUT_FILE = fopen(OUTPUT_FILE_PATH, "w");
    if (OUTPUT_FILE == NULL) {
        fprintf(stderr, "Failed to open %s for writing.\n", OUTPUT_FILE_PATH);
        exit(1);
    }

    // Parse dimensions
    if (sscanf(argv[3], "%d", &GRID_X) != 1 ) {
        fprintf(stderr, "Failed to parse <grid_x> as positive integer. Got '%s'. Aborting...\n", argv[3]);
        exit(1);
    }
    if (sscanf(argv[4], "%d", &GRID_Y) != 1 ) {
        fprintf(stderr, "Failed to parse <grid_y> as positive integer. Got '%s'. Aborting...\n", argv[4]);
        exit(1);
    }
    if (sscanf(argv[5], "%d", &GRID_Z) != 1 ) {
        fprintf(stderr, "Failed to parse <grid_z> as positive integer. Got '%s'. Aborting...\n", argv[5]);
        exit(1);
    }
    if (sscanf(argv[6], "%d", &BLOCK_X) != 1 ) {
        fprintf(stderr, "Failed to parse <block_x> as positive integer. Got '%s'. Aborting...\n", argv[6]);
        exit(1);
    }
    if (sscanf(argv[7], "%d", &BLOCK_Y) != 1 ) {
        fprintf(stderr, "Failed to parse <block_y> as positive integer. Got '%s'. Aborting...\n", argv[7]);
        exit(1);
    }
    if (sscanf(argv[8], "%d", &BLOCK_Z) != 1 ) {
        fprintf(stderr, "Failed to parse <block_z> as positive integer. Got '%s'. Aborting...\n", argv[8]);
        exit(1);
    }

    // Check if dimensions are non-negative
    if (GRID_X < 0) {
        fprintf(stderr, "Failed to parse <grid_x> as positive integer. Got '%s'. Aborting...\n", argv[3]);
        exit(1);
    }
    if (GRID_Y < 0) {
        fprintf(stderr, "Failed to parse <grid_y> as positive integer. Got '%s'. Aborting...\n", argv[4]);
        exit(1);
    }
    if (GRID_Z < 0) {
        fprintf(stderr, "Failed to parse <grid_z> as positive integer. Got '%s'. Aborting...\n", argv[5]);
        exit(1);
    }
    if (BLOCK_X < 0) {
        fprintf(stderr, "Failed to parse <block_x> as positive integer. Got '%s'. Aborting...\n", argv[6]);
        exit(1);
    }
    if (BLOCK_Y < 0) {
        fprintf(stderr, "Failed to parse <block_y> as positive integer. Got '%s'. Aborting...\n", argv[7]);
        exit(1);
    }
    if (BLOCK_Z < 0) {
        fprintf(stderr, "Failed to parse <block_z> as positive integer. Got '%s'. Aborting...\n", argv[8]);
        exit(1);
    }

    dim3 gridDim(GRID_X, GRID_Y, GRID_Z);
    dim3 blockDim(BLOCK_X, BLOCK_Y, BLOCK_Z);

    // Read input file
    char *line = NULL;
    size_t len = 0;
    // Read nGenerations
    if (readParam(INPUT_FILE, &line, &len, &N_GENERATIONS) == -1) {
        fprintf(stderr, "Failed to read N_GENERATIONS. Aborting...\n");
        exit(EXIT_FAILURE);
    }
    // Read nRows
    if (readParam(INPUT_FILE, &line, &len, &N_ROWS) == -1) {
        fprintf(stderr, "Failed to read N_ROWS. Aborting...\n");
        exit(1);
    }

    // Read nCols
    if (readParam(INPUT_FILE, &line, &len, &N_COLS) == -1) {
        fprintf(stderr, "Failed to read N_COLS. Aborting...\n");
        exit(1);
    }

    if (N_ROWS == 0 || N_COLS == 0) {
        fprintf(stderr, "N_ROWS or N_COLS is 0. Aborting...\n");
        exit(1);
    }

    // Read start world
    START_WORLD = (int *) malloc(sizeof(int) * N_ROWS * N_COLS);
    if (START_WORLD == NULL || readWorldLayout(INPUT_FILE, &line, &len, START_WORLD, N_ROWS, N_COLS) == -1)
    {
        fprintf(stderr, "Failed to read START_WORLD. Aborting...\n");
        exit(1);
    }

    // Read nInvasions
    if (readParam(INPUT_FILE, &line, &len, &N_INVASIONS) == -1)
    {
        fprintf(stderr, "Failed to read N_INVASIONS. Aborting...\n");
        exit(1);
    }

    // Read invasions
    INVASION_TIMES = (int *) malloc(sizeof(int) * N_INVASIONS);
    INVASION_PLANS = (int **) malloc(sizeof(int *) * N_INVASIONS);
    if (INVASION_TIMES == NULL || INVASION_PLANS == NULL)
    {
        fprintf(stderr, "No memory for invasions. Aborting...\n");
        exit(1);
    }
    for (int i = 0; i < N_INVASIONS; i++)
    {
        if (INVASION_TIMES == NULL || readParam(INPUT_FILE, &line, &len, INVASION_TIMES + i))
        {
            fprintf(stderr, "Failed to read INVASION_TIME. Aborting...\n");
            exit(1);
        }

        INVASION_PLANS[i] = (int *) malloc(sizeof(int) * N_ROWS * N_COLS);
        if (INVASION_PLANS[i] == NULL || readWorldLayout(INPUT_FILE, &line, &len, INVASION_PLANS[i], N_ROWS, N_COLS))
        {
            fprintf(stderr, "Failed to read INVASION_PLAN. Aborting...\n");
            exit(1);
        }
    }

    // Close input file
    fclose(INPUT_FILE);
    fclose(OUTPUT_FILE);

    // Free line
    if (line) {
        free(line);
    }

    // free everything!
    for (int i = 0; i < N_INVASIONS; i++)
    {
        free(INVASION_PLANS[i]);
    }
    free(INVASION_TIMES);
    free(INVASION_PLANS);
    free(START_WORLD);
}

// readParam reads one integer from a line into param, advancing the read head to the next line.
// -1 is returned on error.
int readParam(FILE *fp, char **line, size_t *len, int *param)
{
    if (getline(line, len, fp) == -1 ||
        sscanf(*line, "%d", param) != 1)
    {
        free(*line);
        return -1;
    }
    return 0;
}

// readWorldLayout reads a world layout specified by nRows and nCols, advancing the read head by
// nRows number of lines. -1 is returned on error.
int readWorldLayout(FILE *fp, char **line, size_t *len, int *world, int nRows, int nCols)
{
    for (int row = 0; row < nRows; row++)
    {
        if (getline(line, len, fp) == -1)
        {
            return -1;
        }

        char *p = *line;
        for (int col = 0; col < nCols; col++)
        {
            char *end;
            int cell = strtol(p, &end, 10);

            // unexpected end
            if (cell == 0 && end == p)
            {
                return -1;
            }

            // other errors
            if (errno == EINVAL || errno == ERANGE)
            {
                return -1;
            }

            setValueAt(world, nRows, nCols, row, col, cell);
            p = end;
        }
    }

    return 0;
}