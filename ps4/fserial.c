/* Author: Keyang Dong, Yale University, Computer Science Dept. */

#include <stdio.h>
#include <string.h>
#include <stddef.h> 
#include <stdlib.h> 
#include <unistd.h> 
#include <math.h>
#include <mpi.h>

#define MAX_N_BODY 50000
#define TIME_INTERVAL 128
#define G 1.0
#define N_DIM 3
#define NP 8
#define CUTOFF_SQR 25.0
#define EPSILON 1.0e-6

int ALL_ONES[] = {1, 1, 1, 1, 1, 1, 1, 1};

int octant_dims[][N_DIM] = {
	{-1, -1, -1}, {1, -1, -1}, {-1, 1, -1}, {1, 1, -1},
	{-1, -1, 1}, {1, -1, 1}, {-1, 1, 1}, {1, 1, 1}
};

typedef struct body_t {
	double mass;
	double x[N_DIM];
	double v[N_DIM];
} Body;

// void sign_of_dims(int rank, int dims[]) {
// 	dims[2] = (rank / 4) ? 1 : -1;
// 	dims[1] = ((rank % 4) / 2) ? 1: -1;
// 	dims[0] = (rank % 2) ? 1 : -1;
// }

int get_octant_rank(double dims[]) {	
	return (dims[2] > 0) * 4 + (dims[1] > 0) * 2 + (dims[0] > 0);
}

void readdata(int *N, int *K, double *DT, Body bodies[], int send_count[], int send_displ[]) {
	int i, sum, i_oct;
	Body bds[MAX_N_BODY];
	int ptrs[NP];

	scanf("%d", N);
	scanf("%d", K);
	scanf("%lf", DT);
	for (i = 0; i < *N; ++ i) scanf("%lf", &bds[i].mass);
	memset(send_count, 0, NP * sizeof(int));
	for (i = 0; i < *N; ++ i) {
		scanf("%lf %lf %lf", &(bds[i].x)[0], &(bds[i].x)[1], &(bds[i].x)[2]);
		i_oct = get_octant_rank(bds[i].x);
		send_count[i_oct] += 1;
	}
	for (i = 0; i < *N; ++ i) scanf("%lf %lf %lf", &(bds[i].v)[0], &(bds[i].v)[1], &(bds[i].v)[2]);
	
	sum = 0;
	for (i = 0; i < NP; ++ i) {
		send_displ[i] = sum;
		sum += send_count[i];
	}  // offset in form of 'num of Body'
	memcpy(ptrs, send_displ, NP * sizeof(int));
	
	// put each body into its octant's slot
	for (i = 0; i < *N; ++ i) {
		i_oct = get_octant_rank(bds[i].x);
		memcpy(&bodies[ptrs[i_oct]], &bds[i], sizeof(Body));
		ptrs[i_oct] += 1;
	}
}

Body get_centroid(int N, int n_bodies[], Body bodies[]) {
	// N is the size of two array parameters
	// bodies[i] represents centroid of n_bodies[i] items
	int i, k, sum_n_body = 0;
	Body centroid;

	centroid.mass = .0;  // centroid's mass is sum rather than avg
	for (k = 0; k < N_DIM; ++ k) centroid.x[k] = centroid.v[k] = .0;
	if (N == 0) return centroid;  // avoid nan

	for (i = 0; i < N; ++ i) {
		sum_n_body += n_bodies[i % NP];  // N may exceed 8, but ALL_ONE is limited to length NP
		centroid.mass += bodies[i].mass;
		for (k = 0; k < N_DIM; ++ k) {
			centroid.x[k] += bodies[i].mass * bodies[i].x[k];
			centroid.v[k] += n_bodies[i % NP] * bodies[i].v[k];
		}
	}

	for (k = 0; k < N_DIM; ++ k) {
		centroid.x[k] /= centroid.mass;
		centroid.v[k] /= sum_n_body;
	}
	return centroid;
}

void report(int t, double DT, int n_bodies[], Body centroids[]) {
	int i;
	Body c;
	c = get_centroid(NP, n_bodies, centroids);
	printf("\n\nConditions after timestep %d (time = %lf) :\n\n", t, t * DT);
	printf("\tCenter of Mass:\t(%e, %e, %e)\n", c.x[0], c.x[1], c.x[2]);
	printf("\tAverage Velocity:\t(%e, %e, %e)\n", c.v[0], c.v[1], c.v[2]);
	printf("\tBodies in octants' distribution:");
	for (i = 0; i < NP; ++ i) printf(" %d", n_bodies[i]);
	printf("\n");
}

// decide whether the body should be transferred to each of the other octants
void shouldSend(double pos[N_DIM], int transmit[NP]) {
	int o, k;
	double dist;

	for (o = 0; o < NP; ++ o) {
		dist = .0;
		for (k = 0; k < N_DIM; ++ k)
			if (octant_dims[o][k] * pos[k] < .0) dist += pow(pos[k], 2);
		if (abs(dist) < EPSILON || dist > CUTOFF_SQR) transmit[o] = 0;
		else transmit[o] = 1;
	}
}

void compute_force_as_vector(const Body *this, const Body *that, double vec[]) {
	// no init of F[] happens inside
	double r2, tmp;
	int k;
	
	if (this == that) return;
	r2 = pow(this->x[0] - that->x[0], 2) + pow(this->x[1] - that->x[1], 2) + pow(this->x[2] - that->x[2], 2);
	if (r2 > CUTOFF_SQR) return;  // too far to have forces between 'em
	tmp = (G * this->mass * that->mass) / pow(r2, 1.5);  // quantative variable
	for (k = 0; k < N_DIM; ++ k) vec[k] += tmp * (that->x[k] - this->x[k]);  // vector variable
}

int main(int argc, char **argv) {
	int N;  // number of bodies
	int K;  // number of time steps
	double DT;  // time step size
	
	double a[N_DIM];  // acceleration in each dimension
	double F[N_DIM];  // force in ea1ch dimension
	int i, j, k, t;
	int sum;  // send sum & recv sum of each process

	MPI_Status status;
	MPI_Datatype mpi_body_t;
	int rank, i_oct;
	int n_body;  // num of bodies that the very process (octant) holds

	Body centroid;  // to store partial centroid of each octant
	Body bodies[MAX_N_BODY];  // bodies that the very process (octant) holds
	int n_bodies[NP];  // overall storage place to collect all n_body from processes
	int transmit[NP];  // flags of whether the body will possibly exert forces on bodies in other octants
	int exile[MAX_N_BODY];  // flags of whether the body will be moved out of current octant's range

	// MPI collective operation supporting data structures
	int send_count[NP];
	int send_displ[NP];
	int recv_count[NP];
	int recv_displ[NP];
	Body sendbuf[MAX_N_BODY];
	Body recvbuf[MAX_N_BODY];
	int ptrs[NP]; // series of pointers that used for filling the send buffer
	
	// variables used to create a new MPI type for easy transfer
	int block_length_array[] = {1, N_DIM, N_DIM};
	MPI_Aint displ_array[] = {offsetof(Body, mass), offsetof(Body, x), offsetof(Body, v)};
	MPI_Datatype types[] = {MPI_DOUBLE, MPI_DOUBLE, MPI_DOUBLE};
	double wct;

	// Environment initialization
	MPI_Init(&argc, &argv); // Required MPI initialization call
	MPI_Comm_rank(MPI_COMM_WORLD, &rank); // Which process am I?

	// create mpi version of Body for transmission
	MPI_Type_create_struct(3, block_length_array, displ_array, types, &mpi_body_t);
	MPI_Type_commit(&mpi_body_t);

	// Read metadata from input stream
	// & distribute bodies to corresponding octants & init send_displ
	readdata(&N, &K, &DT, bodies, n_bodies, send_displ);

	// Root process broadcast metadata to workers
	MPI_Bcast(&N, 1, MPI_INT, 0, MPI_COMM_WORLD);
	MPI_Bcast(&K, 1, MPI_INT, 0, MPI_COMM_WORLD);
	MPI_Bcast(&DT, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);

	MPI_Scatter(n_bodies, 1, MPI_INT, &n_body, 1, MPI_INT, 0, MPI_COMM_WORLD);
	MPI_Scatterv(bodies, n_bodies, send_displ, mpi_body_t, 
		recvbuf, n_body, mpi_body_t, 0, MPI_COMM_WORLD);

	MPI_Barrier(MPI_COMM_WORLD);
	memcpy(bodies, recvbuf, n_body * sizeof(Body));  // now we can treat recvbuf as released

	// Main calculation loop
	wct = MPI_Wtime();
	for (t = 0; t < K; ++ t) {
		if (t % TIME_INTERVAL == 0) { // centroid collect & report body distribution
			centroid = get_centroid(n_body, ALL_ONES, bodies);
			MPI_Gather(&n_body, 1, MPI_INT, n_bodies, 1, MPI_INT, 0, MPI_COMM_WORLD);
			MPI_Gather(&centroid, 1, mpi_body_t, recvbuf, 1, mpi_body_t, 0, MPI_COMM_WORLD);
			if (rank == 0) report(t, DT, n_bodies, recvbuf);
		}

		/* Prepare sending bodies within 5DU to other octants */
		memset(send_count, 0, NP * sizeof(int));

		// Compute send_count & set slots for send buffer fulfilling
		for (i = 0; i < n_body; ++ i) {
			shouldSend(bodies[i].x, transmit);
			for (j = 0; j < NP; ++ j) {
				if (transmit[j]) send_count[j] += 1;
			}
		}

		sum = 0;  // used as send_sum
		for (j = 0; j < NP; ++ j) {
			send_displ[j] = sum;
			sum += send_count[j];
		}
		memcpy(ptrs, send_displ, NP * sizeof(int)); // ptr init

		// Send buffer fulfilling
		for (i = 0; i < n_body; ++ i) {
			shouldSend(bodies[i].x, transmit);
			for (j = 0; j < NP; ++ j) {
				if (transmit[j]) {
					memcpy(&sendbuf[ptrs[j]], &bodies[i], sizeof(Body));
					ptrs[j] += 1;
				}
			}
		}

		// Notice each other about send_count as recv_count
		MPI_Barrier(MPI_COMM_WORLD);
		MPI_Alltoall(send_count, 1, MPI_INT, recv_count, 1, MPI_INT, MPI_COMM_WORLD);
		MPI_Barrier(MPI_COMM_WORLD);

		// Constitutes recv_displ
		sum = 0;  // used as recv_sum
		for (j = 0; j < NP; ++ j) {
			recv_displ[j] = sum;
			sum += recv_count[j];
		}

		// Sending actual bodies that might be useful to other octants
		MPI_Alltoallv(sendbuf, send_count, send_displ, mpi_body_t,
			recvbuf, recv_count, recv_displ, mpi_body_t, MPI_COMM_WORLD);

		MPI_Barrier(MPI_COMM_WORLD);
		// flush send_count; otherwise empty octant will cause bizarre results
		memset(send_count, 0, NP * sizeof(int));

		// update status of bodies within the octant
		for (i = 0; i < n_body; ++ i) {
			F[0] = F[1] = F[2] = .0;
			// forces within the octant
			for (j = 0; j < n_body; ++ j) 
				compute_force_as_vector(&bodies[i], &bodies[j], F);  // accumulates vec variable F
			// forces outside of the current octant
			for (j = 0; j < sum; ++ j)
				compute_force_as_vector(&bodies[i], &recvbuf[j], F);
			// scientific formula computing
			for (k = 0; k < N_DIM; ++ k) {
				bodies[i].v[k] += F[k] * (DT / 2.0) / bodies[i].mass;
				bodies[i].x[k] += bodies[i].v[k] * DT; 
			}

			// check if the body is still within reign of current octant
			i_oct = get_octant_rank(bodies[i].x);
			if (i_oct != rank) {  // need to be sent to octant [rank = i_oct]
				send_count[i_oct] += 1;  // set size of slots for sendbuf
				exile[i] = i_oct;  // set a flag in exile array
			} else exile[i] = -1;
		}

		/* Communicate with each other to exchange bodies beyond their own reigns */
		// Prepare slots for sendbuf
		sum = 0; // send sum
		for (j = 0; j < NP; ++ j) {
			send_displ[j] = sum;
			sum += send_count[j];
		}
		
		memcpy(ptrs, send_displ, NP * sizeof(int));

		// Send buffer fulfilling
		for (i = 0; i < n_body; ++ i) {
			i_oct = exile[i];
			if (i_oct < 0) continue;
			memcpy(&sendbuf[ptrs[i_oct]], &bodies[i], sizeof(Body));
			ptrs[i_oct] += 1;
		}

		// Notice each other about send_count as recv_count
		MPI_Barrier(MPI_COMM_WORLD);
		MPI_Alltoall(send_count, 1, MPI_INT, recv_count, 1, MPI_INT, MPI_COMM_WORLD);
		MPI_Barrier(MPI_COMM_WORLD);

		// Constitutes recv_displ
		sum = 0; // recv sum
		for (j = 0; j < NP; ++ j) {
			recv_displ[j] = sum;
			sum += recv_count[j];
		}

		// Sending actual bodies
		MPI_Alltoallv(sendbuf, send_count, send_displ, mpi_body_t,
			recvbuf, recv_count, recv_displ, mpi_body_t, MPI_COMM_WORLD);
		MPI_Barrier(MPI_COMM_WORLD);

		// put each received body into missing slots
		i = sum;
		for (j = 0; j < n_body; ++ j) {
			if (exile[j] < 0) {
				memcpy(&recvbuf[i], &bodies[j], sizeof(Body));
				i += 1;
			}
		}
		memcpy(bodies, recvbuf, i * sizeof(Body));
		// printf("Octant %d : %d + %d -> %d\n", rank, n_body, sum, n_body + sum);
		n_body = i;
	}
	wct = MPI_Wtime() - wct;

	// final centroid collect & report body distribution & timing information
	centroid = get_centroid(n_body, ALL_ONES, bodies);
	MPI_Gather(&n_body, 1, MPI_INT, n_bodies, 1, MPI_INT, 0, MPI_COMM_WORLD);
	MPI_Gather(&centroid, 1, mpi_body_t, recvbuf, 1, mpi_body_t, 0, MPI_COMM_WORLD);
	if (rank == 0) {
		report(t, DT, n_bodies, recvbuf);
		printf("Time for %d timesteps with %d bodies\t%lf seconds\n", K, N, wct);
	}

	MPI_Type_free(&mpi_body_t);
	MPI_Finalize();
}