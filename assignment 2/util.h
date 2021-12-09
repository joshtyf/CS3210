extern "C" {
    extern int getValueAt(const int *grid, int nRows, int nCols, int row, int col);
    extern void setValueAt(int *grid, int nRows, int nCols, int row, int col, int val);
    extern void printWorld(const int *world, int nRows, int nCols);
}

