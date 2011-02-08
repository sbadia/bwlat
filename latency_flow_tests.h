/*
 * MPI LATENCY FLOW TESTS
 *
 * Cree une matrice des debits et latence entre chaque noeud (sauf le rank 0) de l'execution MPI, dans les deux sens,
 * ou separe la liste des noeuds en deux pour faire une bissection.
 *
 * OPTIONS : Voir -h
 *
 * Copyright (C) 2010 Julien VAUBOURG / Sébastien BADIA
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef LATENCY_FLOW_TESTS
#define LATENCY_FLOW_TESTS

#define DEACTIVATED -1
#define RECVER 0
#define SENDER 1
#define MASTER 0

typedef struct {
	int sender, recver;
	float latency, flow;
} Bench;

typedef struct {
	int role, withRank;
} YourTest;

typedef struct {
	char myHostname[100];
	Bench result;
} MyResult;

typedef struct {
	Bench *min, *max;
	float sum, avg;
} StatsResult;

MPI_Datatype BenchType, TestType, ResultType;
MPI_Datatype benchTypeTypes[4] = { MPI_INT, MPI_INT, MPI_FLOAT, MPI_FLOAT };
MPI_Datatype testTypeTypes[2] = { MPI_INT, MPI_INT };
MPI_Aint benchTypeDisp[4], testTypeDisp[2], resultTypeDisp[2], extentType;
MPI_Status status;

int benchTypeBlocks[4] = { 1, 1, 1, 1 };
int testTypeBlocks[2] = { 1, 1 };
int resultTypeBlocks[2] = { 100, 1 };
int* buffer;

void initOptions(int argc, char** argv, int nbNodes, int rank, int* pktSize, int* nbRetry, int* bissection, int* randBiss, int* gnuplot, int* yaml, char* yamlFile);

void createBenchType();
void createTestType();
void createResultType();

void prepareTests(int nbNodes, int sender);
void launchTests(int sender, int recver);
void waitTests(YourTest* t);
void sendResults(MyResult* r);
void receiveResults(MyResult* r, int sender);

void formatTestsResult(MyResult* r, YourTest* t, int rank);
void benchTests(YourTest* t, Bench* r, int pktSize);
void responsesToTests(YourTest* t, int pktSize);

void bissPrepareAllTests(YourTest* bissTests, int nbNodes);
void bissPrepareAllRandTests(YourTest* bissTests, int nbNodes);
void bissTransmitAllTests(YourTest* bissTests, YourTest* t);
void bissLaunchAllTests();
void bissTransmitAllResults(MyResult* bissResults, MyResult* r);

char* rankToHostname(MyResult** r, MyResult* rBiss, int rank, int bissection);

void displayTab(MyResult** r, MyResult* rBiss, int bissection, int nbNodes);
void toYAML(MyResult** r, MyResult* rBiss, char* yamlFile, int nbNodes, int bissection);
void toGnuplot(StatsResult* flowStats, int nbNodes);

void stats(MyResult** r, MyResult* rBiss, StatsResult* latencyStats, StatsResult* flowStats, int nbNodes, int bissection);
void displayStats(MyResult** r, MyResult* rBiss, StatsResult* latencyStats, StatsResult* flowStats, int nbNodes, int bissection);

void freee(int* buffer, MyResult* bissResults, YourTest* bissTests, MyResult** benchResults, Bench* sameBenchs, int nbNodes, int nbRetry);

#endif
