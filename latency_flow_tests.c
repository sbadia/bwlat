/*
 * OAR LATENCY FLOW TESTS
 *
 * Cree une matrice des debits et latence entre chaque noeud (sauf le rank 0) de l'execution MPI, dans les deux sens,
 * ou separe la liste des noeuds en deux pour faire une bissection.
 *
 * OPTIONS : Voir -h
 *
 * AUTEURS : <julien@vaubourg.com>
 *	     <seb@sebian.fr> <badia.seb@gmail.com>
 *
 * 2010, pour Grid5000
 *
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <mpi.h>

#include "latency_flow_tests.h"

/* A compiler avec un compilateur MPI et a executer avec une commande de la meme categorie, sur une reservation de 3 noeuds ou plus */
int main(int argc, char** argv) {
	int
		rank, /* Numero du noeud qui execute le script, par rapport au nb de noeuds concernes par l'execution */
		nbNodes, /* Nombre de noeuds concernes par l'execution du programme */
		pktSize, /* Taille du mot qui sera envoye pour faire les tests de debit */
		nbRetry, /* Nb de fois qu'un test sera recommence afin d'augmenter la precision */
		bissection, /* Mode bissection ? */
		randBiss, /* Mode bissection, avec formation des paires aleatoires ? */
		yaml, /* Sortie dans un fichier YAML (yamlFile) en plus de la matrice ? */
		gnuplot, /* Sortie pour un graphique gnuplot plutot qu'une matrice ? */
		i, sender, recver, l; /* Divers compteurs */
	float
		sumLatency, /* Somme de toutes les latences d'un meme test afin de pouvoir faire la moyenne */
		sumFlow; /* Idem pour le debit */
	YourTest
		*bissTests, /* Tableau de tous les tests a envoyer, sert pour le rank 0 */
		myTest; /* Test que recevra le noeud si il n'est pas le rank 0 */
	Bench
		*sameBenchs; /* Tableau qui recevra tous les resultats des benchs d'un meme test, a partir desquels on fera des moyennes */
	MyResult
		myResult, /* Resultat que fabriquera le noeud a partir de son test, si il n'est pas le rank 0 */
		*bissResults, /* Tableau final contenant tous les resultats des tests, dont l'indice indique le rank du sender du test */
		**benchResults; /* Idem mais a deux dimensions : en y le sender, en x le receveur. Ne sert aussi que pour le rank 0 */
	StatsResult
		latencyStats, /* Pointeurs vers les benchs ayant enregistres les latences min et max, ainsi que la somme de toutes les latences et la moyenne */
		flowStats; /* Idem pour les debits */
	char
		yamlFile[50]; /* Nom du fichier qui accueillera la sortie YAML si l'option -o est passee */


	/* Initialisation des connexions MPI et recuperation du nb de noeuds concernes par l'execution
	ainsi que le numero - rang - du noeud courant courant */
	MPI_Init(&argc, &argv);
    	MPI_Comm_size(MPI_COMM_WORLD, &nbNodes);
    	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	/* Prise en compte des differentes options passées au script */
	initOptions(argc, argv, nbNodes, rank, &pktSize, &nbRetry, &bissection, &randBiss, &gnuplot, &yaml, yamlFile);

	/*  Le buffer sert pour envoyer ou recevoir le mot qui sert de test au debit */
	buffer = (int *) malloc(sizeof(int)*pktSize);
	l = 0;

	if(buffer == NULL) {
		fprintf(stderr, "ERROR: Can't allocate memory.");
		exit(1);
	}

	/* Creation des structures qui pourront dorenavant transiter d'un noeud a l'autre avec MPI */
	createBenchType();
	createTestType();
	createResultType();

	/* Le MASTER est le rank 0, c'est lui qui enverra les tests, qui recevra les resultats et qui les affichera.
	Il ne fait pas parti des tests. */
	if(rank == MASTER) {

		/* Le mode bissection (option -b) consiste a creer des paires de noeuds dans ceux faisant parti de l'execution du programme
		et faire envoyer un mot d'un noeud a l'autre, en demarrant tous en meme tps */
		if(bissection) {
			bissTests = (YourTest*) malloc(sizeof(YourTest)*nbNodes);
			bissResults = (MyResult*) malloc(sizeof(MyResult)*nbNodes);

			if(bissTests == NULL || bissResults == NULL) {
				fprintf(stderr, "ERROR: Can't allocate memory.");
				exit(1);
			}

			/* Si l'option -rb est active, alors les formations de paires se feront aleatoirement parmis les noeuds dispo */
			if(randBiss)
				bissPrepareAllRandTests(bissTests, nbNodes);

			/* Sinon, elles se font en coupant le nombre de noeuds en deux et en prenant le premier de la premiere moitie et le
			premier de la seconde, et ainsi de suite */
			else
				bissPrepareAllTests(bissTests, nbNodes);

			/* Transmission des tests prepares aux interesses : c'est une fonction collective, ce qui signifie que tous les noeuds
			doivent avoir execute cette meme fonction, et etre en attente de cet appel du rank 0 (sinon, ils seront attendus).
			Une fois que chacun a recu son test, chacun connait son role dans la transaction : envoyeur ou receveur. Chacun se met
			en ecoute du rank 0 pour savoir quand il devra commencer a jouer son role. */
			bissTransmitAllTests(bissTests, &myTest);

			/* Chaque noeud est en ecoute de cette meme fonction (puisque c'est aussi une fonction collective). Elle envoi un mot vide
			en broadcast a tous pour leur dire que le jeu debute : les receveurs se mettent a ecouter les envoyeurs, et les envoyeurs
			envoient aux receveurs. Chacun se remet ensuite en ecoute du rank 0, et ce jusqu'a ce que la precision imposee par nbRetry
			soit atteinte. */
			for(i = 0; i < nbRetry; i++)
				bissLaunchAllTests();

			/* Derniere fonction collective : tout le monde envoi son resultat, seuls les resultats des envoyeurs auront de l'interet */
			bissTransmitAllResults(bissResults, &myResult);

		/* Le contraire du mode bissection est le mode matrice : chaque noeud enverra un noeud a tous les autres, afin de pouvoir creer
		une matrice complete des performances entre tous les noeuds concerne par l'execution, dans tous les sens */
		} else {

			benchResults = (MyResult**) malloc(sizeof(MyResult*)*nbNodes);

			if(benchResults == NULL) {
				fprintf(stderr, "ERROR: Can't allocate memory.");
				exit(1);
			}

			for(i = 0; i < nbNodes; i++) {
				benchResults[i] = (MyResult*) malloc(sizeof(MyResult)*nbNodes);

				if(benchResults[i] == NULL) {
					fprintf(stderr, "ERROR: Can't allocate memory.");
					exit(1);
				}
			}

			for(sender = 1; sender < nbNodes; sender++) {

				/* Preparation des tests pour le noeud envoyeur : mise en ecoute de ce noeud, pour tous les autres
				noeuds qui recevront tour a tour un mot de lui. Chacun de ces noeuds recoit donc un test de receveur */
				prepareTests(nbNodes, sender);

				for(recver = 1; recver < nbNodes; recver++) {
					if(recver != sender) {

						/* Envoi d'un test a l'envoyeur, lui indiquant de communiquer avec le receveur courant */
						launchTests(sender, recver);

						/* Reception du resultat du test, directement dans la matrice des resultats */
						receiveResults(&benchResults[sender][recver], sender);
					}
				}
			}
		}

		/* Calcul des statistiques */
		stats(benchResults, bissResults, &latencyStats, &flowStats, nbNodes, bissection);

		/* Ecriture d'un fichier YAML si l'option -o est passee */
		if(yaml)
			toYAML(benchResults, bissResults, yamlFile, nbNodes, bissection);

		/* Sortie en coordonnees pour un graphique gnuplot des debits selon des bissections */
		if(gnuplot) {
			toGnuplot(&flowStats, nbNodes);

		/* Sinon matrice + stats */
		} else {

			/* Affichage d'un tableau/matrice non-parsable sur la sortie standard */
			displayTab(benchResults, bissResults, bissection, nbNodes);

			/* Affichage des statistiques sur la sortie standard */
			displayStats(benchResults, bissResults, &latencyStats, &flowStats, nbNodes, bissection);
		}

	/* Si le noeud qui execute le programme n'est pas le MASTER (rank != 0), alors il sera charge de participer aux tests
	qui lui enverra le MASTER, et de lui en renvoyer les resultats.
	Dans le cas d'une bissection, chaque noeud n'aura qu'un seul role dans sa vie (envoyeur ou receveur), alors que dans le
	cas de matrice, chacun des noeuds autant de fois envoyeur qu'il y a de noeud, et autant de fois receveur. A l'exclusion,
	chaque fois, du rank 0, d'ou le -2 */
	} else while(l++ < (bissection ? 1 : nbNodes*2-4)) {

		sameBenchs = (Bench *) malloc(sizeof(Bench)*nbRetry);

		if(sameBenchs == NULL) {
			fprintf(stderr, "ERROR: Can't allocate memory.");
			exit(1);
		}

		/* Si c'est une bissection, la fonction collective est utilise pour recevoir le test en meme tps que tout le monde */
		if(bissection)
			bissTransmitAllTests(bissTests, &myTest);

		/* Sinon, le noeud est simplement en ecoute d'un test sur le rank 0 */
		else
			waitTests(&myTest);

		/* Formatage du type MyResult qui sera renvoye, en renseignant le hostname du noeud courant, les deux joueurs de ce tests
		et les valeurs du flow et debit initialisees a -1 */
		formatTestsResult(&myResult, &myTest, rank);

		switch(myTest.role) {

			/* Le test recu du rank 0 designe le noeud temporairement comme envoyeur */
			case SENDER :

				/* Les tests avec le noeud receveur se repeteront autant de fois que l'indication de precision
				nbRetry l'impose */
				for(i = 0; i < nbRetry; i++) {

					/* Si c'est une bissection, la fonction collective de lancement des tests est rappellee a chaque fois.
					Ceci permet d'etre assure que tout le monde recommence bien son test au meme moment. Sans cela, les couples
					qui auraient pris du retard sur le premier test se retrouveront seuls dans les derniers tests, lorsque les
					plus rapides les auront tous finis. Il seront donc moins ralentis pour ces derniers tests, qui fausseront
					leur moyenne. */
					if(bissection)
						bissLaunchAllTests();

					/* Envoi du mot vide pour la latence, reception du resultat, envoi du mot de pktSize octets pour le debit,
					reception du resultat.
					Les differences de temps entre chaque envoi et reponse permettent de calculer la latence et le debit, qui
					seront stockes dans le tableau des benchs de ce test, passe en parametre en ecriture. */
					benchTests(&myTest, &sameBenchs[i], pktSize);
				}

				sumLatency = sumFlow = 0;

				/* Calcul des sommes pour etablir une moyenne de tous les resultats du meme test */
				for(i = 0; i < nbRetry; i++) {
					sumLatency += sameBenchs[i].latency;
					sumFlow += sameBenchs[i].flow;
				}

				/* Calcul des moyennes et initialisation des valeurs du MyResult qui sera renvoye au MASTER */
				myResult.result.latency = sumLatency / nbRetry;
				myResult.result.flow = sumFlow / nbRetry;

				/* Si ca n'est pas une bissection, renvoi direct des resultats au MASTER */
				if(!bissection)
					sendResults(&myResult);

			break;

			/* Le role du desactive intervient dans un unique cas de figure : en mode bissection, si le nombre de noeud, en l'enlevant le rank 0,
			est impaire. Le noeud du rank le plus eleve recoit donc ce role, qui le fera participer aux fonction collectives, mais qui ne sera pas
			pris en compte. Tous les noeuds ne naissent pas libres et egaux... */
			case DEACTIVATED :

			/* Cas d'un receveur */
			case RECVER :

				/* Le receveur recevra autant de fois que nbRetry l'impose, parce que l'envoyeur enverra tout autant de fois */
				for(i = 0; i < nbRetry; i++) {

					/* Si c'est une bissection, la reception est bloquee tant qu'un nouveau depart de synchro n'a pas ete donne
					par le MASTER */
					if(bissection)
						bissLaunchAllTests();

					/* Si on est pas dans le cas d'un exclu, reponse aux deux tests successifs de l'envoyeur partenaire */
					if(myTest.role != DEACTIVATED)
						responsesToTests(&myTest, pktSize);
				}
		}

		/* Dans le cas d'une bissection, tous les resultats sont envoyes en meme temps au MASTER, a travers une fonction collective.
		Les resultats des receveurs ou du desactive ne seront pas pris en compte. */
		if(bissection)
			bissTransmitAllResults(bissResults, &myResult);
	}

	//freee(buffer, bissResults, bissTests, benchResults, sameBenchs, nbNodes, nbRetry);
	
	MPI_Finalize();

	return 0;
}


/*********************
 ***** FUNCTIONS *****
 *********************/

/*
 * Gestion des options du script
 */
void initOptions(int argc, char** argv, int nbNodes, int rank, int* pktSize, int* nbRetry, int* bissection, int* randBiss, int* gnuplot, int* yaml, char* yamlFile) {
	char opt, unit;

	/* La taille par defaut du mot envoye pour les tests de debit est 1M */
	*pktSize = 1024 * 1024;

	/* Le nombre de fois par defaut qu'un test est repete pour ameliore la precision des resultats est 10 */
	*nbRetry = 10;

	/* Par defaut, la bissection (et a forciori la bissection aleatoire) ainsi que le yaml sont desactives */
	*bissection = *randBiss = *gnuplot = *yaml = 0;

	while((opt = getopt(argc, argv, "hs:p:bro:g")) != -1) {
		switch(opt) {

			/* Help */
			case 'h' :
				if(rank == MASTER) {
					puts("\nOAR FLOW LATENCY TESTS");
					puts("------------------------");
					puts("\t-s <n>K   : Message size for flow tests exchanges, in bytes (with K, M, or G suffix). Min 64K, default 1M.");
					puts("\t-p <n>    : Tests precision (repeats each test <n> times and make averages). Default 10.");
					puts("\t-b        : Bisection (first node of the first half with the first node of the seconde half, and so on).");
					puts("\t-rb, -r   : Bisection, with random pairs.");
					puts("\t-bg, -g   : Bisection, with gnuplot coordinates output.");
					puts("\t-o <file> : YAML output.\n");
					puts("\t-h        : This help.\n");
					puts("AUTHORS : <julien@vaubourg.com>\n          <sebastien.badia@gmail.com>\n");
				}

				MPI_Finalize();
				exit(0);
			break;

			/* Taille des mots qui seront envoyes pour les tests de debit, en octets et avec un suffix (K, M, G) */
			case 's' :

				/* Recuperation de l'unite utilisee en recuperant le dernier caractere de la valeur (nK, nM, ou nG), puis suppression de celle-ci */
				unit = optarg[strlen(optarg) - 1];
				*(strchr(optarg, unit)) = '\0';
				*pktSize = atoi(optarg);

				/* En l'absence de break intermediaires, la taille du mot sera multipliee par autant de fois qu'il faudra traverser une unite
				subalterne pour atteindre le break final. */
				switch(unit) {
					case 'G' :
						*pktSize *= 1024;
					case 'M' :
						*pktSize *= 1024;
					case 'K' :
						*pktSize *= 1024;
					break;

					default :
						if(rank == MASTER)
							fprintf(stderr, "ERROR: Unit -s unknown (K, M, or G).");

						exit(1);
				}

				/* Un calcul de debit ne peut pas se faire avec un mot de moins de 64K (tests NWS) */
				if(*pktSize < 64*1024) {
					if(rank == MASTER)
						fprintf(stderr, "ERROR: For realistic flows results, the message size defined by -s must be greater than 64KB (NWS method).");

					exit(1);
				}
			break;

			/* Mode bissection : couples aleatoires si -r, en fonction de la moitiee de la liste des noeuds sinon */
			case 'r' :
				*randBiss = 1;
			case 'b' :
				*bissection = 1;
			break;

			/* Sortie pour un graphique gnuplot */
			case 'g' :
				*gnuplot = 1;
				*bissection = 1;
			break;

			/* Sortie dans un fichier YAML en plus de la sortie matrice */
			case 'o' :
				*yaml = 1;
				strncpy(yamlFile, optarg, 50);
			break;

			/* Les tests se feront autant de fois que l'indicateur de precision -p le dit, en prenant la moyenne des resultats de tous */
			case 'p' :
				*nbRetry = atoi(optarg);

				if(*nbRetry < 0) {
					if(rank == MASTER)
						fprintf(stderr, "ERROR: The -p option must be positive.");

					exit(1);
				}
			break;

			/* Option inconnue ou mal renseignee */
			case '?' :
				if(rank == MASTER) {
					if(optopt == 's' || optopt == 'r' || optopt == 'o')
						fprintf(stderr, "ERROR: The -%c option require an argument.\n", optopt);
					else if(isprint(optopt))
						fprintf(stderr, "ERROR: The -%c option is unknown.\n", optopt);
					else
						fprintf(stderr, "ERROR: The '\\x%x' character is unknown.\n", optopt);
				}
			default:
				exit(1);
		}
	}

	if(nbNodes < 3) {
		if(rank == MASTER)
			fprintf(stderr, "ERROR : Tests requires at least 3 nodes (1 for monitoring, at least 2 for exchanges).");

		exit(1);
	}
}

/*
 * Creation d'un type BenchType pour MPI qui permettra de faire transiter des structures Bench d'un noeud a l'autre.
 */
void createBenchType() {
	MPI_Type_extent(MPI_INT, &extentType);

	benchTypeDisp[0] = 0;
	benchTypeDisp[1] = extentType;
	benchTypeDisp[2] = extentType*2;

	MPI_Type_extent(MPI_FLOAT, &extentType);

	benchTypeDisp[3] = benchTypeDisp[2] + extentType;

	MPI_Type_struct(4, benchTypeBlocks, benchTypeDisp, benchTypeTypes, &BenchType);
	MPI_Type_commit(&BenchType);
}

/*
 * Creation d'un type TestType pour MPI qui permettra de faire transiter des structures YourTest d'un noeud a l'autre.
 */
void createTestType() {
	MPI_Type_extent(MPI_INT, &extentType);

	testTypeDisp[0] = 0;
	testTypeDisp[1] = extentType;

	MPI_Type_struct(2, testTypeBlocks, testTypeDisp, testTypeTypes, &TestType);
	MPI_Type_commit(&TestType);
}

/*
 * Creation d'un type ResultType pour MPI qui permettra de faire transiter des structures MyResult d'un noeud a l'autre.
 */
void createResultType() {
	MPI_Datatype resultTypeTypes[2] = { MPI_CHAR, BenchType };
	MPI_Type_extent(MPI_CHAR, &extentType);

	resultTypeDisp[0] = 0;
	resultTypeDisp[1] = extentType*100;

	MPI_Type_struct(2, resultTypeBlocks, resultTypeDisp, resultTypeTypes, &ResultType);
	MPI_Type_commit(&ResultType);
}

/*
 * Met en ecoute tous les autres noeuds que l'envoyeur designe, afin qu'ils soient prets a recevoir un test de celui-ci.
 */
void prepareTests(int nbNodes, int sender) {
	int i;
	YourTest t;

	for(i = 1; i < nbNodes; i++) {
		if(i != sender) {
			t.role = RECVER;
			t.withRank = sender;

			MPI_Send(&t, 1, TestType, i, 0, MPI_COMM_WORLD);
		}
	}
}

/*
 * Envoi de son test a l'envoyeur pour lui indiquer de communiquer avec un des receveurs pret a receptionner un mot
 * de lui.
 */
void launchTests(int sender, int recver) {
	YourTest t;

	t.role = SENDER;
	t.withRank = recver;

	MPI_Send(&t, 1, TestType, sender, 0, MPI_COMM_WORLD);
}

/*
 * Par defaut, tous les noeuds attendent un test du MASTER.
 */
void waitTests(YourTest* t) {
	MPI_Recv(t, 1, TestType, MASTER, 0, MPI_COMM_WORLD, &status);
}

/*
 * Envoi du resultat MyResult au MASTER, de la part de l'envoyeur.
 */
void sendResults(MyResult* r) {
	MPI_Send(r, 1, ResultType, MASTER, 0, MPI_COMM_WORLD);
}

/*
 * Reception du resultat MyResult de l'envoyeur qui vient de realiser son test (pour le rank 0).
 */
void receiveResults(MyResult* r, int sender) {
	MPI_Recv(r, 1, ResultType, sender, 0, MPI_COMM_WORLD, &status);
}

/* Formatage du MyResult qui sera renvoye au MASTER */
void formatTestsResult(MyResult* r, YourTest* t, int rank) {
	int sizeHostname;
	char* sep;

	/* Les deux joueurs sont renseignes, et la latence est mise a -1 : si le test n'est
	jamais utilise, la latence restera ainsi, et pourra alors etre un indicateur de la non
	pertinence du resultat. */
	r->result.sender = rank;
	r->result.recver = t->withRank;
	r->result.latency = r->result.flow = -1;

	/* Renseignement du nom du noeud courant */
	MPI_Get_processor_name(r->myHostname, &sizeHostname);

	/* Reduction du nom du noeud a sa premiere partie (on vire .site.grid5000.fr) */
	sep = strchr(r->myHostname, '.');
	*sep = '\0';
}

/*
 * Tests de debit/latence de l'envoyeur vers le receveur.
 */
void benchTests(YourTest* t, Bench* r, int pktSize) {
	double start, stop;

	/* Un mot vide (4 octets) est envoye au receveur, qui repondra immediatement un mot de la meme nature.
	Le temps est compte, de l'envoi du mot au receveur jusqu'a la reception de sa reponse. */
	start = MPI_Wtime();
	MPI_Send(buffer, 0, MPI_BYTE, t->withRank, 1, MPI_COMM_WORLD);
	MPI_Recv(buffer, 0, MPI_BYTE, t->withRank, 1, MPI_COMM_WORLD, &status);
	stop = MPI_Wtime();

	/* La latence est calculee en fonction du temps mis par le mot pour arriver au destinataire (division par 2
	pour eliminer le tps de reponse qui est cense etre identique.
	Passage de seconde a microseconde en multiplisant par un million. */
	r->latency = ((stop-start) / 2) * 1e6;

	/* Un mot plus ou moins consequent (option -s) est envoye au receveur. Celui-ci renvoyant un mot vide.
	Le temps est egalement compte, de l'envoi jusqu'a la reception. */
	start = MPI_Wtime();
	MPI_Send(buffer, pktSize, MPI_BYTE, t->withRank, 1, MPI_COMM_WORLD);
	MPI_Recv(buffer, 0, MPI_BYTE, t->withRank, 1, MPI_COMM_WORLD, &status);
	stop = MPI_Wtime();

	/* La difference de temps est prise en compte. On lui soustraie deux fois la latence - en seconde grace a la
	division - d'un mot vide (soit le tps que met le tuyau a faire transiter les donnees, quelque soit la taille des
	donnees). Enfin, on divise le nombre d'octets qui ont transites avec ce resultat. On divise le tout par 1024 au
	carre pour avoir des Mo/s au lieu de o/s. Cette facon de proceder releve de l'approche NWS : http://nws.cs.ucsb.ed */
	r->flow = pktSize / ((stop-start) - (2*r->latency/1e6)) / pow(1024, 2);
}

/*
 * Reponses automatiques aux envoi du noeud qui joue le role d'envoyeur.
 */
void responsesToTests(YourTest* t, int pktSize) {
	MPI_Recv(buffer, 0, MPI_BYTE, t->withRank, 1, MPI_COMM_WORLD, &status);
	MPI_Send(buffer, 0, MPI_BYTE, t->withRank, 1, MPI_COMM_WORLD);

	MPI_Recv(buffer, pktSize, MPI_BYTE, t->withRank, 1, MPI_COMM_WORLD, &status);
	MPI_Send(buffer, 0, MPI_BYTE, t->withRank, 1, MPI_COMM_WORLD);
}

/*
 * Creation de tous les tests qui seront repartis entre tous les noeuds, pour le mode bissection, sans les envoyer et en les
 * stockant dans un tableau.
 */
void bissPrepareAllTests(YourTest* bissTests, int nbNodes) {
	int m, i;
	YourTest t;

	/* Le rank 0 ne participe pas */
	nbNodes--;

	/* Si il y a un nb impaire de noeud, on ignore le dernier, en le faisant quand meme participer
	aux fonctions collectives*/
	if(nbNodes % 2) {
		bissTests[nbNodes].role = DEACTIVATED;
		bissTests[nbNodes].withRank = -1;
		nbNodes--;
	}

	m = nbNodes / 2;

	/* Pour chaque paire */
	for(i = 1; i <= m; i++) {

		/* Le receveur de la paire est le n ieme de la seconde moitie */
		bissTests[m+i].role = RECVER;
		bissTests[m+i].withRank = i;

		/* L'envoyeur est le n ieme de la premiere moitie */
		bissTests[i].role = SENDER;
		bissTests[i].withRank = m+i;
	}
}

/*
 * Creations des tests pour la bissection, en creant les paires de noeuds de facon aleatoire (sans envoyer les tests,
 * et en les stockant dans un tableau.
 */
void bissPrepareAllRandTests(YourTest* bissTests, int nbNodes) {
	int i, r, randRank, ranks[2], iLeft, *ranksLeft, nbLeft;
	YourTest t;

	/* Le rank 0 ne participe pas */
	nbNodes--;

	/* Si il y a un nb impaire de noeud, on ignore le dernier, en le faisant quand meme participer
	aux fonctions collectives*/
	if(nbNodes % 2) {
		bissTests[nbNodes].role = DEACTIVATED;
		bissTests[nbNodes].withRank = -1;
		nbNodes--;
	}

	/* Nombre et liste des noeuds qui n'ont pas encore de test attribue */
	nbLeft = nbNodes;
	ranksLeft = (int*) malloc(sizeof(int) * nbLeft);

	if(ranksLeft == NULL) {
		fprintf(stderr, "ERROR: Can't allocate memory.");
		exit(1);
	}

	/* Initialisation des noeuds : avec la valeur 1, chacun est encore vaquant */
	for(i = 0; i < nbLeft; ranksLeft[i++] = 1);

	/* Pour tous les noeuds vaquants restants */
	while(nbLeft != 0) {

		/* Extraction de deux noeuds choisis aleatoirement parmis les noeuds encore disponibles
		Premier tour pour l'envoyeur, second tour pour le receveur */
		for(i = 0; i < 2; i++) {
			srand(time(NULL));

			/* Tirage d'un nombre aleatoire qui definira le rank du noeud choisi, parmis les ranks restants.
			Ex: si 3 est tire, le rank selectionne sera le 3ieme qui n'a pas encore ete utilise */
			randRank = rand() % (nbLeft-1);

			r = -1;
			iLeft = 0;
			ranks[i] = -1;

			/* Pour tous les ranks jusqu'a ce que le n ieme rank encore vaquant soit atteint */
			while(ranks[i] == -1) {

				/* Si le rank en cours est encore vaquant */
				if(ranksLeft[++r] != -1) {

					/* Si le rank en cours correspond au rank du tirage aleatoire, selection de ce rank (+1, puisque le rank 0 a ete supprime) */
					if(iLeft == randRank)
						ranks[i] = r + 1;

					/*  Sinon incrementation du nb des ranks encore vaquants deja rencontres */
					iLeft++;
				}
			}

			/* Ce rank ne pourra plus etre selectionne */
			ranksLeft[r] = -1;
		}

		/* Deux ranks en moins a attribuer */
		nbLeft -= 2;

		/* Creation du test du receveur, qui attendra alors un message de son camarade de jeu */
		bissTests[ranks[1]].role = RECVER;
		bissTests[ranks[1]].withRank = ranks[0];

		/* Creation du test de l'envoyeur */
		bissTests[ranks[0]].role = SENDER;
		bissTests[ranks[0]].withRank = ranks[1];
	}
}

/*
 * Fonction collective d'envoi du test associe a chaque rank, depuis le MASTER.
 */
void bissTransmitAllTests(YourTest* bissTests, YourTest* t) {
	MPI_Scatter(bissTests, 1, TestType, t, 1, TestType, MASTER, MPI_COMM_WORLD);
}

/*
 * Fonction collective d'envoi d'un broadcast vide, permettant de lancer un depart synchro des tests de bissections.
 */
void bissLaunchAllTests() {
	MPI_Bcast(buffer, 0, MPI_BYTE, MASTER, MPI_COMM_WORLD);
}

/*
 * Fonction collectice d'envoi des resultats de la part de chaque rank vers le MASTER.
 */
void bissTransmitAllResults(MyResult* bissResults, MyResult* r) {
	MPI_Gather(r, 1, ResultType, bissResults, 1, ResultType, MASTER, MPI_COMM_WORLD);
}

/* Fonction permettant la conversion numero de rank vers Hostname. */
char* rankToHostname(MyResult** r, MyResult* rBiss, int rank, int bissection) {
	if(bissection)
		return rBiss[rank].myHostname;
	else
		return r[rank][rank == 1 ? 2 : 1].myHostname;
}

/*
 * Affichage des resultats sous forme de tableau texte non-parsable.
 */
void displayTab(MyResult** r, MyResult* rBiss, int bissection, int nbNodes) {
	int x, y, i;

	/* Affichage des resultats de la bissections sous forme d'un tableau simple dont la premier colonne indique
	l'envoyeur et la seconde le receveur avec la latence et le debit calculees */
	if(bissection) {

		/* Pour chaque envoyeur */
		for(i = 1; i < nbNodes; i++) {

			/* La latence differente de -1 signifie que le MyResult a ete exploite et n'est pas le retour d'un
			RECVER ou d'un DEACTIVATED */
			if(rBiss[i].result.latency != -1) {
				puts("+---------------------+----------------------+");
				printf("| From %-15s", rankToHostname(r, rBiss, i, bissection));
				printf("| To %-17s |\n", rankToHostname(r, rBiss, rBiss[i].result.recver, bissection));
				printf("|                     ");
				printf("| %17.3f us |\n", rBiss[i].result.latency);
				printf("|                     ");
				printf("| %15.3f Mo/s |\n", rBiss[i].result.flow);
			}
		}

		puts("+---------------------+----------------------+");

	/* Affichage des resultats sous forme d'une matrice equilibree de tous les noeuds, avec la debit et la latence pour chaque
	paire de noeud possible, dans les deux sens */
	} else {

		/* Premiere ligne de tirets */
		printf("                       ");
		for(x = 1; x < nbNodes; printf("+----------------------"), x++);
		printf("+\n                       ");

		/* Lignes des entetes de colonnes (receveurs) */
		for(x = 1; x < nbNodes; printf("| To %-17s ", rankToHostname(r, rBiss, x++, bissection)));
		printf("|\n");

		/* Ligne de tirets de separation */
		for(x = 0; x < nbNodes; printf("+----------------------"), x++);
		puts("+");

		/* Pour chaque ligne/envoyeur */
		for(y = 1; y < nbNodes; y++) {

			/* Entete */
			printf("| From %-15s ", r[y][y == 1 ? 2 : 1].myHostname);

			/* Latence, ou suite de tirets si on est l'envoyeur correspond au receveur */
			for(x = 1; x < nbNodes; x++) {
				(x == y) ?
					printf("|----------------------") :
					printf("| %17.3f us ", r[y][x].result.latency);
			}

			printf("|\n|                      ");

			/* Debit, ou suite de tirets si on est l'envoyeur correspond au receveur */
			for(x = 1; x < nbNodes; x++) {
				(x == y) ?
					printf("|----------------------") :
					printf("| %15.3f Mo/s ", r[y][x].result.flow);
			}

			/* Fermeture de ligne */
			puts("|");
			for(x = 1; x < nbNodes+1; printf("+----------------------"), x++);
			puts("+");
		}
	}
}
/* 
 * Affichage des statistiques en relation avec la matrice ou la bissection sous forme de texte non-parsable.
 * Latence min et max ainsi que debit min et max.
 * Ainsi que les sommes et les moyennes.
 */
void displayStats(MyResult** r, MyResult* rBiss, StatsResult* latencyStats, StatsResult* flowStats, int nbNodes, int bissection) {

	puts("\nLatency :");

	/* Latence Maximum et affichage de la paire de noeuds correspondante, en hostname */
	printf(
		"Max : %.3f us \tFrom %s to %s\n", 
		latencyStats->max->latency, 
		rankToHostname(r, rBiss, latencyStats->max->sender, bissection), 
		rankToHostname(r, rBiss, latencyStats->max->recver, bissection)
	);
	
	/* Latence Minimum et affichage de la paire de noeuds correspondante, en hostname */
        printf(
		"Min : %.3f us \tFrom %s to %s\n",
		latencyStats->min->latency, 
		rankToHostname(r, rBiss, latencyStats->min->sender, bissection), 
		rankToHostname(r, rBiss, latencyStats->min->recver, bissection)
	);

	/* Somme des latences et moyenne */
	printf("Sum : %.3f us\n", latencyStats->sum);
	printf("Avg : %.3f us\n", latencyStats->avg);

        puts("\nFlow :");

	/* Debit Maximum et affichage de la paire de noeuds correspondante, en hostname */
        printf(
		"Max : %.3f Mo/s \tFrom %s to %s\n", 
		flowStats->max->flow, 
		rankToHostname(r, rBiss, flowStats->max->sender, bissection), 
		rankToHostname(r, rBiss, flowStats->max->recver, bissection)
	);

	/* Debit Minimum et affichage de la paire de noeuds correspondante, en hostname */
        printf(
		"Min : %.3f Mo/s \tFrom %s to %s\n", 
		flowStats->min->flow, 
		rankToHostname(r, rBiss, flowStats->min->sender, bissection), 
		rankToHostname(r, rBiss, flowStats->min->recver, bissection)
	);
	
	/* Somme des debits et moyenne */
	printf("Sum : %.3f Mo/s\n", flowStats->sum);
        printf("Avg : %.3f Mo/s\n", flowStats->avg);
}

/*
 * Ecriture en YAML dans un fichier des resultats.
 */
void toYAML(MyResult** r, MyResult* rBiss, char* yamlFile, int nbNodes, int bissection) {
	FILE* yaml;
	int x, y, i;

	yaml = fopen(yamlFile, "w");

	if(yaml == NULL) {
		fprintf(stderr, "ERROR: Can't write the yaml file.");
		return;
	}

	/* Export du tableau des resultats (meme structure que pour la matrice) */
	if(bissection) {

		/* Pour chaque envoyeur */
		for(y = 1; y < nbNodes; y++) {

			/* Resultat d'un envoyeur */
			if(rBiss[y].result.latency != -1) {
				fprintf(yaml, "%s\n", rankToHostname(r, rBiss, y, bissection));
				fprintf(yaml, "  %s\n", rankToHostname(r, rBiss, rBiss[y].result.recver, bissection));
				fprintf(yaml, "    latency ; %.3f\n", rBiss[y].result.latency);
				fprintf(yaml, "    flow : %.3f\n", rBiss[y].result.flow);
			}
		}

	/* Export de la matrice des resultats (3 niveaux : envoyeur => receveur => debit/latence) */
	} else {

		/* Pour chaque envoyeur */
		for(y = 1; y < nbNodes; y++) {
			fprintf(yaml, "%s :\n", rankToHostname(r, rBiss, y, bissection));

			/* Pour chaque receveur */
			for(x = 1; x < nbNodes; x++) {
				if(y != x) {
					fprintf(yaml, "  %s :\n", rankToHostname(r, rBiss, x, bissection));
					fprintf(yaml, "    latency : %.3f\n", r[y][x].result.latency);
					fprintf(yaml, "    flow : %.3f\n", r[y][x].result.flow);
				}
			}
		}
	}

	fclose(yaml);
}

/*
 * Exporte la somme des debits constates sous forme de coordonnees (x: nb noeuds, y: somme).
 * Sert pour la construction d'un graphique gnuplot du total des debits de bissections testees avec des nombres de noeuds
 * variables, et dans le cadre d'un script plus global.
 */
void toGnuplot(StatsResult* flowStats, int nbNodes) {
	printf("%d\t%.3f\n", nbNodes, flowStats->sum);
}

/*
 * Fonction de calcul des statistiques. 
 */
void stats(MyResult** r, MyResult* rBiss, StatsResult* latencyStats, StatsResult* flowStats, int nbNodes, int bissection) {
	int i, j;
	Bench initLatencyMin, initLatencyMax, initFlowMin, initFlowMax;
	
	/* Initialisation de la structure benchsStatsBounds */
	latencyStats->min = &initLatencyMin;
	latencyStats->min->latency = 99999999.99;

	latencyStats->max = &initLatencyMax;
	latencyStats->max->latency = -1;
	
	flowStats->min = &initFlowMin;
	flowStats->min->flow = 99999999.99;

	flowStats->max = &initFlowMax;
	flowStats->max->flow = -1;
	
	/* Initialisation des sommes */
	latencyStats->sum = flowStats->sum = 0;

	if(bissection) {
		for (i = 1; i < nbNodes; i++) {

			/* Si la latence est encore en etat d'initialisation on arrete */
			if(rBiss[i].result.latency != -1) {

				/* Calcul de la Latence Minimum et attribution de l'adresse correspondante */
				if (rBiss[i].result.latency < (latencyStats->min->latency)) {
					latencyStats->min = &(rBiss[i].result);
				}

				/* Calcul de la Latence Maximum et attribution de l'adresse correspondante */
				if (rBiss[i].result.latency > (latencyStats->max->latency)) {
					latencyStats->max = &(rBiss[i].result);
				}

				/* Calcul du debit Minimum et attribution de l'adresse correspondante */
				if (rBiss[i].result.flow < (flowStats->min->flow)) {
					flowStats->min = &(rBiss[i].result);
				}

				/* Calcul du debit Maximum et attribution de l'adresse correspondante */
				if (rBiss[i].result.flow > (flowStats->max->flow)) {
					flowStats->max = &(rBiss[i].result);
				} 

				/* Calcul de la somme des debits */
				flowStats->sum += rBiss[i].result.flow;
				
				/* Calcul de la somme des latence */
				latencyStats->sum += rBiss[i].result.latency;
			}
		}
		
		/* Calculs des Moyennes */

		/* Debit */
		flowStats->avg = flowStats->sum / ((nbNodes-1)/2);

		/* Latence */
		latencyStats->avg = latencyStats->sum / ((nbNodes-1)/2);

	} else {

		/* Double boucle afin de parcourir l'ensemble de la matrice */
		for (i = 1; i < nbNodes; i++) {
			for (j = 1; j < nbNodes; ++j) {

				/* On evite le cas particulier ou le rank est egal (case vide) */
				if(i != j) {

					/* Calcul de la Latence Minimum et attribution de l'adresse correspondante */
					if (r[i][j].result.latency < (latencyStats->min->latency)) {
						latencyStats->min = &(r[i][j].result);
					}

					/* Calcul de la Latence Maximum et attribution de l'adresse correspondante */
					if (r[i][j].result.latency > (latencyStats->max->latency)) {
						latencyStats->max = &(r[i][j].result);
					}
					
					/* Calcul du debit Minimum et attribution de l'adresse correspondante */
					if (r[i][j].result.flow < (flowStats->min->flow)) {
						flowStats->min = &(r[i][j].result);
					}
		
					/* Calcul du debit Maximum et attribution de l'adresse correspondante */
					if (r[i][j].result.flow > (flowStats->max->flow)) {
						flowStats->max = &(r[i][j].result);
					} 

					/* Calcul de la somme des debits */
					flowStats->sum += r[i][j].result.flow;
				
					/* Calcul de la somme des latence */
					latencyStats->sum += r[i][j].result.latency;
				}
			}
		}

		/* Calcul des Moyennes et enregistrement */

		/* Debit */
		flowStats->avg = flowStats->sum / (pow((nbNodes-1),2)-(nbNodes-1));

		/* Latence */
		latencyStats->avg = latencyStats->sum / (pow((nbNodes-1),2)-(nbNodes-1));
	}
}

/*
 * Liberation de la memoire pour les allocations faites manuellement.
 */
void freee(int* buffer, MyResult* bissResults, YourTest* bissTests, MyResult** benchResults, Bench* sameBenchs, int nbNodes, int nbRetry) {
	int i;

	free(buffer);
	free(bissResults);
	free(bissTests);
	free(sameBenchs);

	for(i = 0; i < nbNodes; free(benchResults[i++]));
	free(benchResults);
}
