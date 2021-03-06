#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <omp.h>
#include <string.h>

#define N 32768

struct BodySet { 
  float x[N], y[N], z[N];
  float vx[N], vy[N], vz[N]; 
};

typedef struct {
  float x, y, z;
} Centroid;

void MoveBodies(const int nBodies, struct BodySet* const bodies, const float dt) {
  omp_set_num_threads(8);
  // Avoid singularity and interaction with self
  const float softening = 1e-20;
  
  #pragma omp parallel for // private(i, j, dx, dy, dz, drSquared, drReciRooted, drPower23) schedule(dynamic)
  // Loop over bodies that experience force
  for (int i = 0; i < nBodies; i++) {
    float Fx, Fy, Fz;  // Components of the gravity force on body i
    Fx = Fy = Fz = .0;
    // Loop over bodies that exert force: vectorization expected here
    for (int j = 0; j < nBodies; j++) { 
      // Newton's law of universal gravity
      const float dx = bodies->x[j] - bodies->x[i];
      const float dy = bodies->y[j] - bodies->y[i];
      const float dz = bodies->z[j] - bodies->z[i];
      const float drSquared  = dx*dx + dy*dy + dz*dz + softening;
      const float drRooted = sqrtf(drSquared);
      const float drPower32 = drRooted * drSquared;
      const float drPower23 = 1.0f / drPower32;
	
      // Calculate the net force
      Fx += dx * drPower23;  
      Fy += dy * drPower23;  
      Fz += dz * drPower23;
    }

    // Accelerate bodies in response to the gravitational force
    bodies->vx[i] += dt * Fx; 
    bodies->vy[i] += dt * Fy; 
    bodies->vz[i] += dt * Fz;
  }
  // end of parallel section
  #pragma omp barrier

  #pragma omp parallel for schedule(dynamic)
  for (int i = 0; i < nBodies; i++) {
    // Move bodies according to their velocities
    bodies->x[i] += bodies->vx[i] * dt;
    bodies->y[i] += bodies->vy[i] * dt;
    bodies->z[i] += bodies->vz[i] * dt;
  }
}

Centroid get_centroid(const int nBodies, struct BodySet* const bodies) {
  float comx = 0.0f, comy=0.0f, comz=0.0f;
  Centroid c;

  #pragma omp parallel for reduction(+:comx)
    for (int i = 0; i < nBodies; ++ i) comx += bodies->x[i];
  #pragma omp parallel for reduction(+:comy)
    for (int i = 0; i < nBodies; ++ i) comy += bodies->y[i];
  #pragma omp parallel for reduction(+:comz)
    for (int i = 0; i < nBodies; ++ i) comz += bodies->z[i];
  
  c.x = comx / nBodies;
  c.y = comy / nBodies;
  c.z = comz / nBodies;
  return c;
}

int main(const int argc, const char** argv) {
  omp_set_num_threads(8);
  // Problem size and other parameters
  const int nBodies = (argc > 1 ? atoi(argv[1]) : 16384);
  const int nSteps = 10;  // Duration of test
  const float dt = 0.01f; // Body propagation time step
  Centroid c;

  // Body data stored as an Array of Structures (AoS)
  struct BodySet bodies;

  // Initialize random number generator and bodies
  srand(0);
  float randmax;
  randmax = (float) RAND_MAX;
  for(int i = 0; i < nBodies; i++) {
    bodies.x[i] = ((float) rand())/randmax; 
    bodies.y[i] = ((float) rand())/randmax; 
    bodies.z[i] = ((float) rand())/randmax; 
    bodies.vx[i] = ((float) rand())/randmax; 
    bodies.vy[i] = ((float) rand())/randmax; 
    bodies.vz[i] = ((float) rand())/randmax; 
  }

  // Compute initial center of mass  
  c = get_centroid(nBodies, &bodies);
  printf("Initial center of mass: (%g, %g, %g)\n", c.x, c.y, c.z);

  // Perform benchmark
  printf("\n\033[1mNBODY Version 03\033[0m\n");
  printf("\nPropagating %d bodies using %d thread on %s...\n\n", 
	 nBodies, omp_get_num_threads(), "CPU");

  double rate = 0, dRate = 0; // Benchmarking data
  const int skipSteps = 3; // Set this to a positive int to skip warm-up steps

  printf("\033[1m%5s %10s %10s %8s\033[0m\n", "Step", "Time, s", "Interact/s", "GFLOP/s"); fflush(stdout);

  for (int step = 1; step <= nSteps; step++) {

    const double tStart = omp_get_wtime(); // Start timing
    MoveBodies(nBodies, &bodies, dt);
    const double tEnd = omp_get_wtime(); // End timing

    // These are for calculating flop rate. It ignores symmetry and 
    // estimates 20 flops per body-body interaction in MoveBodies
    const float HztoInts   = (float)nBodies * (float)(nBodies-1) ;
    const float HztoGFLOPs = 20.0*1e-9*(float)nBodies*(float)(nBodies-1);

    if (step > skipSteps) { 
      // Collect statistics 
      rate  += HztoGFLOPs / (tEnd - tStart); 
      dRate += HztoGFLOPs * HztoGFLOPs / ((tEnd-tStart)*(tEnd-tStart)); 
    }

    printf("%5d %10.3e %10.3e %8.1f %s\n", 
	   step, (tEnd-tStart), HztoInts/(tEnd-tStart), HztoGFLOPs/(tEnd-tStart), (step<=skipSteps?"*":""));
    fflush(stdout);
  }

  rate/=(double)(nSteps-skipSteps); 
  dRate=sqrt(fabs(dRate/(double)(nSteps-skipSteps)-rate*rate));

  printf("-----------------------------------------------------\n");
  printf("\033[1m%s %4s \033[42m%10.1f +- %.1f GFLOP/s\033[0m\n",
	 "Average performance:", "", rate, dRate);
  printf("-----------------------------------------------------\n");
  printf("* - warm-up, not included in average\n\n");

  // Compute final center of mass
  c = get_centroid(nBodies, &bodies);
  printf("Final center of mass: (%g, %g, %g)\n", c.x, c.y, c.z);

}
