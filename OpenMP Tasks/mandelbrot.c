/*
  Jonathan Armknecht CS324 Sec 2 Task Version
  Compute Times for tasks as a row
    1 thread 44 seconds
    2 threads 23 seconds
    4 threads 17 seconds
    8 threads 16 seconds

  all done with running this command prompt
    ./mandelbrot 0.27085 0.27100 0.004640 0.004810 1000 8192 pic.ppm



  This program is an adaptation of the Mandelbrot program
  from the Programming Rosetta Stone, see
  http://rosettacode.org/wiki/Mandelbrot_set
  Compile the program with:
  gcc -o mandelbrot -O4 mandelbrot.c
  Usage:
 
  ./mandelbrot <xmin> <xmax> <ymin> <ymax> <maxiter> <xres> <out.ppm>
  Example:
  ./mandelbrot 0.27085 0.27100 0.004640 0.004810 1000 1024 pic.ppm
  The interior of Mandelbrot set is black, the levels are gray.
  If you have very many levels, the picture is likely going to be quite
  dark. You can postprocess it to fix the palette. For instance,
  with colorMagick you can do (assuming the picture was saved to pic.ppm):
  convert -normalize pic.ppm pic.png
  The resulting pic.png is still gray, but the levels will be nicer. You
  can also add colors, for instance:
  convert -negate -normalize -fill blue -tint 100 pic.ppm pic.png
  See http://www.colormagick.org/Usage/color_mods/ for what colorMagick
  can do. It can do a lot.
*/

#include <time.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>

typedef unsigned char PIXEL[6];
const static int NUM_THREADS = 8;

int main(int argc, char *argv[])
{
  /* Parse the command line arguments. */
  if (argc != 8)
  {
    printf("Usage:   %s <xmin> <xmax> <ymin> <ymax> <maxiter> <xres> <out.ppm>\n", argv[0]);
    printf("Example: %s 0.27085 0.27100 0.004640 0.004810 1000 1024 pic.ppm\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  /* The window in the plane. */
  const double xmin = atof(argv[1]);
  const double xmax = atof(argv[2]);
  const double ymin = atof(argv[3]);
  const double ymax = atof(argv[4]);

  /* Maximum number of iterations, at most 65535. */
  const uint16_t maxiter = (unsigned short)atoi(argv[5]);

  /* Image size, width is given, height is computed. */
  const int xres = atoi(argv[6]);
  const int yres = (xres * (ymax - ymin)) / (xmax - xmin);

  /* Precompute pixel width and height. */
  double dx = (xmax - xmin) / xres;
  double dy = (ymax - ymin) / yres;

  /* Make a pixel map so when concurrent writing occurs it will be printed out correctly */
  PIXEL **color = malloc(yres * sizeof(PIXEL *));
  color[0] = malloc(yres * xres * sizeof(PIXEL));

  for (int q = 1; q < yres; q++)
  {
    color[q] = color[0] + (q * xres);
  }

  double x, y; /* Coordinates of the current point in the complex plane. */
  int k;       /* Iteration counter */

  time_t startTime = time(NULL); //for computing the time taken
  printf("Starting computation\n");

  omp_set_num_threads(NUM_THREADS);

#pragma omp parallel private(k, x, y)
  #pragma omp single
    for (int j = 0; j < yres; j++)
    {
      y = ymax - j * dy;
      #pragma omp task
        for (int i = 0; i < xres; i++)
        {
          double u = 0.0;
          double v = 0.0;
          double u2 = u * u;
          double v2 = v * v;
          x = xmin + i * dx;
          /* iterate the point */
          for (k = 1; k < maxiter && (u2 + v2 < 4.0); k++)
          {
            v = 2 * u * v + y;
            u = u2 - v2 + x;
            u2 = u * u;
            v2 = v * v;
          };
          /* compute  pixel color and write it to file */
          if (k >= maxiter)
          {
            for (int z = 0; z < 6; z++)
            {
              color[j][i][z] = 0;
            }
          }
          else
          {
            color[j][i][0] = k >> 8;
            color[j][i][1] = k & 255;
            color[j][i][2] = k >> 8;
            color[j][i][3] = k & 255;
            color[j][i][4] = k >> 8;
            color[j][i][5] = k & 255;
          };
        }
    }

  time_t endTime = time(NULL);
  printf("Calculations done in %f seconds with %d threads.\nWriting to file. . .\n", difftime(endTime, startTime), NUM_THREADS);
  /* The output file name */
  const char *filename = argv[7];

  /* Open the file and write the header. */
  FILE *fp = fopen(filename, "wb");
  char *comment = "# Mandelbrot set"; /* comment should start with # */

  /*write ASCII header to the file*/
  fprintf(fp,
          "P6\n# Mandelbrot, xmin=%lf, xmax=%lf, ymin=%lf, ymax=%lf, maxiter=%d\n%d\n%d\n%d\n",
          xmin, xmax, ymin, ymax, maxiter, xres, yres, (maxiter < 256 ? 256 : maxiter));

  //copy color array to the file
  for (int j = 0; j < yres; j++) {
    for (int i = 0; i < xres; i++) {
      fwrite(color[j][i], 6, 1, fp);
    }
  }
  fclose(fp);
  free(*color);
  free(color);
  return 0;
}