/*
 * trans.c - Matrix transpose B = A^T
 *
 * Each transpose function must have a prototype of the form:
 * void trans(int M, int N, int A[N][M], int B[M][N]);
 *
 * A transpose function is evaluated by counting the number of misses
 * on a 1KB direct mapped cache with a block size of 32 bytes.
 */
#include <stdio.h>

#include "cachelab.h"

int is_transpose(int M, int N, int A[N][M], int B[M][N]);

/*
 * transpose_submit - This is the solution transpose function that you
 *     will be graded on for Part B of the assignment. Do not change
 *     the description string "Transpose submission", as the driver
 *     searches for that string to identify the transpose function to
 *     be graded.
 */
char transpose_submit_desc[] = "Transpose submission";
void transpose_submit(int M, int N, int A[N][M], int B[M][N]) {
    if (M == 32 && N == 32) {
        int x0, x1, x2, x3, x4, x5, x6, x7, i, j, t, k;
        for (i = 0; i < 32; i += 8) {
            for (j = 0; j < 32; j += 8) {
                for (t = 0; t < 8; t++) {
                    x0 = A[i + t][j];
                    x1 = A[i + t][j + 1];
                    x2 = A[i + t][j + 2];
                    x3 = A[i + t][j + 3];
                    x4 = A[i + t][j + 4];
                    x5 = A[i + t][j + 5];
                    x6 = A[i + t][j + 6];
                    x7 = A[i + t][j + 7];
                    B[j + t][i] = x0;
                    B[j + t][i + 1] = x1;
                    B[j + t][i + 2] = x2;
                    B[j + t][i + 3] = x3;
                    B[j + t][i + 4] = x4;
                    B[j + t][i + 5] = x5;
                    B[j + t][i + 6] = x6;
                    B[j + t][i + 7] = x7;
                }
                for (t = 0; t < 8; t++) {
                    for (k = t + 1; k < 8; k++) {
                        x0 = B[t + j][k + i];
                        B[t + j][k + i] = B[k + j][t + i];
                        B[k + j][t + i] = x0;
                    }
                }
            }
        }
    } else if (M == 64 && N == 64) {
        int i, j, m, n, x0, x1, x2, x3, x4, x5, x6, x7;
        for (i = 0; i < 64; i += 8) {
            for (j = 0; j < 64; j += 8) {
                for (m = i; m < i + 4; m++) {
                    x0 = A[m][j];
                    x1 = A[m][j + 1];
                    x2 = A[m][j + 2];
                    x3 = A[m][j + 3];
                    x4 = A[m][j + 4];
                    x5 = A[m][j + 5];
                    x6 = A[m][j + 6];
                    x7 = A[m][j + 7];
                    B[j][m] = x0;
                    B[j + 1][m] = x1;
                    B[j + 2][m] = x2;
                    B[j + 3][m] = x3;
                    B[j][m + 4] = x4;
                    B[j + 1][m + 4] = x5;
                    B[j + 2][m + 4] = x6;
                    B[j + 3][m + 4] = x7;
                }
                for (n = j; n < j + 4; n++) {
                    x0 = A[i + 4][n];
                    x1 = A[i + 5][n];
                    x2 = A[i + 6][n];
                    x3 = A[i + 7][n];
                    x4 = B[n][i + 4];
                    x5 = B[n][i + 5];
                    x6 = B[n][i + 6];
                    x7 = B[n][i + 7];
                    B[n][i + 4] = x0;
                    B[n][i + 5] = x1;
                    B[n][i + 6] = x2;
                    B[n][i + 7] = x3;
                    B[n + 4][i] = x4;
                    B[n + 4][i + 1] = x5;
                    B[n + 4][i + 2] = x6;
                    B[n + 4][i + 3] = x7;
                }
                for (m = i + 4; m < i + 8; m++) {
                    x0 = A[m][j + 4];
                    x1 = A[m][j + 5];
                    x2 = A[m][j + 6];
                    x3 = A[m][j + 7];
                    B[j + 4][m] = x0;
                    B[j + 5][m] = x1;
                    B[j + 6][m] = x2;
                    B[j + 7][m] = x3;
                }
            }
        }
    } else if (M == 61 && N == 67) {
        int i, j, m, n;
        for (i = 0; i < 67; i += 23) {
            for (j = 0; j < 61; j += 23) {
                for (m = i; m < 67 && m < i + 23; m++) {
                    for (n = j; n < 61 && n < j + 23; n++) {
                        B[n][m] = A[m][n];
                    }
                }
            }
        }
    }
}

/*
 * You can define additional transpose functions below. We've defined
 * a simple one below to help you get started.
 */

/*
 * trans - A simple baseline transpose function, not optimized for the cache.
 */
char trans_desc[] = "Simple row-wise scan transpose";
void trans(int M, int N, int A[N][M], int B[M][N]) {
    int i, j, tmp;

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; j++) {
            tmp = A[i][j];
            B[j][i] = tmp;
        }
    }
}

/*
 * registerFunctions - This function registers your transpose
 *     functions with the driver.  At runtime, the driver will
 *     evaluate each of the registered functions and summarize their
 *     performance. This is a handy way to experiment with different
 *     transpose strategies.
 */
void registerFunctions() {
    /* Register your solution function */
    registerTransFunction(transpose_submit, transpose_submit_desc);

    /* Register any additional transpose functions */
    registerTransFunction(trans, trans_desc);
}

/*
 * is_transpose - This helper function checks if B is the transpose of
 *     A. You can check the correctness of your transpose by calling
 *     it before returning from the transpose function.
 */
int is_transpose(int M, int N, int A[N][M], int B[M][N]) {
    int i, j;

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; ++j) {
            if (A[i][j] != B[j][i]) {
                return 0;
            }
        }
    }
    return 1;
}
