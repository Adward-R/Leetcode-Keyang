#!/bin/bash
#PBS -l procs=8,tpn=8,mem=34gb
#PBS -l walltime=30:00
#PBS -N Task4
#PBS -r n
#PBS -j oe
#PBS -q fas_devel

module load Langs/Intel/15 MPI/OpenMPI/1.8.6-intel15
pwd
cd $PBS_O_WORKDIR
pwd
cat $PBS_NODEFILE
make clean
make task4
time mpiexec -n 4 ./task4 1000
# time mpiexec -n 2 ./task4 2000
# time mpiexec -n 4 ./task4 4000
# time mpiexec -n 8 ./task4 8000
