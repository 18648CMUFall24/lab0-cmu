#!/bin/bash

PAPP=./papp
MGR=./rsvmgr

NUM_MANY=15

setcancel() {
    $PAPP 100 200 &
    PID=$!
    $MGR set $PID 110 200
    sleep 2
    $MGR cancel $PID
}

test_one() {
    setcancel
}

test_many_seq() {

for i in `seq NUM_MANY`
do
    setcancel
done

}

test_many_conc() {

for i in `seq NUM_MANY`
do
    setcancel &
done

}

$1

