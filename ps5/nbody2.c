#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <omp.h>
#include <string.h>

struct BodyType { 
  float x, y, z;
  float vx, vy, vz; 
};

typedef struct {
  float x, y, z;
} Centroid;

void MoveBodies(const int nBodies, struct BodyType* const bodies, const float dt) {

  // Avoid singularity and interaction with self
  const float softening = 1e-20;
  // Components of the gravity force on body i
  float Fx[nBodies], Fy[nBodies], Fz[nBodies];
  int i, j;

  memset(Fx, 0, nBodies * sizeof(float));
  memset(Fy, 0, nBodies * sizeof(float));
  memset(Fz, 0, nBodies * sizeof(float));
  
  #pragma omp parallel for private(i, j) schedule(dynamic)
  // Loop over bodies that experience force
  for (i = 0; i < nBodies; i++) {
    // Loop over bodies that exert force: vectorization expected here
    for (j = 0; j < nBodies; j++) { 
      
      // Newton's law of universal gravity
      const float dx = bodies[j].x - bodies[i].x;
      const float dy = bodies[j].y - bodies[i].y;
      const float dz = bodies[j].z - bodies[i].z;
      const float drSquared  = dx*dx + dy*dy + dz*dz + softening;
      const float drReciRooted = 1.0f / sqrtf(drSquared);  // all expensive operations happened here
      const float drPower23 = drReciRooted * drReciRooted * drReciRooted;
	
      // Calculate the net force
      Fx[i] += dx * drPower23;  
      Fy[i] += dy * drPower23;  
      Fz[i] += dz * drPower23;
    }
  }
  // end of parallel section
  #pragma omp barrier

  #pragma omp parallel for schedule(dynamic)
  for (int i = 0; i < nBodies; i++) {
    // Accelerate bodies in response to the gravitational force
    bodies[i].vx += dt * Fx[i]; 
    bodies[i].vy += dt * Fy[i]; 
    bodies[i].vz += dt * Fz[i];

    // Move bodies according to their velocities
    bodies[i].x += bodies[i].vx * dt;
    bodies[i].y += bodies[i].vy * dt;
    bodies[i].z += bodies[i].vz * dt;
  }
}

Centroid get_centroid(const int nBodies, struct BodyType* const bodies) {
  float comx = 0.0f, comy=0.0f, comz=0.0f;
  Centroid c;

  #pragma omp parallel for reduction(+:comx)
    for (int i = 0; i < nBodies; ++ i) comx += bodies[i].x;
  #pragma omp parallel for reduction(+:comy)
    for (int i = 0; i < nBodies; ++ i) comy += bodies[i].y;
  #pragma omp parallel for reduction(+:comz)
    for (int i = 0; i < nBodies; ++ i) comz += bodies[i].z;
  
  c.x = comx / nBodies;
  c.y = comy / nBodies;
  c.z = comz / nBodies;
  return c;
}

int main(const int argc, const char** argv) {
  // omp_set_num_threads(8);
  // Problem size and other parameters
  const int nBodies = (argc > 1 ? atoi(argv[1]) : 16384);
  const int nSteps = 10;  // Duration of test
  const float dt = 0.01f; // Body propagation time step
  Centroid c;

  // Body data stored as an Array of Structures (AoS)
  struct BodyType bodies[nBodies];

  // Initialize random number generator and bodies
  srand(0);
  float randmax;
  randmax = (float) RAND_MAX;
  for(int i = 0; i < nBodies; i++) {
    bodies[i].x = ((float) rand())/randmax; 
    bodies[i].y = ((float) rand())/randmax; 
    bodies[i].z = ((float) rand())/randmax; 
    bodies[i].vx = ((float) rand())/randmax; 
    bodies[i].vy = ((float) rand())/randmax; 
    bodies[i].vz = ((float) rand())/randmax; 
  }

  // Compute initial center of mass  
  c = get_centroid(nBodies, bodies);
  printf("Initial center of mass: (%g, %g, %g)\n", c.x, c.y, c.z);

  // Perform benchmark
  printf("\n\033[1mNBODY Version 02\033[0m\n");
  printf("\nPropagating %d bodies using %d thread on %s...\n\n", 
	 nBodies, omp_get_num_threads(), "CPU");

  double rate = 0, dRate = 0; // Benchmarking data
  const int skipSteps = 3; // Set this to a positive int to skip warm-up steps

  printf("\033[1m%5s %10s %10s %8s\033[0m\n", "Step", "Time, s", "Interact/s", "GFLOP/s"); fflush(stdout);

  for (int step = 1; step <= nSteps; step++) {

    const double tStart = omp_get_wtime(); // Start timing
    MoveBodies(nBodies, bodies, dt);
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
  c = get_centroid(nBodies, bodies);
  printf("Final center of mass: (%g, %g, %g)\n", c.x, c.y, c.z);

}
