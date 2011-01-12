#!/bin/bash

if [[ -z "$OAR_NODEFILE" ]]; then
	echo "ERROR : You must have a nodes reservation (oarsub, on a single cluster)." >&2
	exit 1
fi

if [[ -z $1 ]]; then
	echo "Usage: ./flowsSumsBis.sh <step>" >&2
	exit 1
fi

STEP=$1
MACHINES=/tmp/${USER}_machines
PLOTFILE=flowsSumsBis.dat
i=3

head -n1 $OAR_NODEFILE > $MACHINES
sort -u $OAR_NODEFILE >> $MACHINES
NB=$(cat $MACHINES | wc -l)
:> $PLOTFILE

if [[ $NB -lt 3 ]]; then
	echo "ERROR : Tests requires at least 3 nodes (1 for monitoring, at least 2 for exchanges)." >&2
	exit 1
fi

while [[ $i -le $NB ]]; do
	echo "Bisection : calculating the flow sum with $i nodes..."
	mpirun -np $i --mca plm_rsh_agent oarsh -machinefile $MACHINES ./latency_flow_tests -g 2> /dev/null >> $PLOTFILE
	i=$(( i + STEP ))
done

rm $MACHINES
exit 0
