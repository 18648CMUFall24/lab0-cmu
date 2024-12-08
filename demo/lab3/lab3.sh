#!/sbin/bash

TOOLS_DIR=tools

STATUS_FILE=/sys/rtes/reserves
ENABLE_TASKMON_FILE=/sys/rtes/taskmon/enabled
CANCEL_RESERVE=1
TASKMON_PATH=/sys/rtes/taskmon

CPUS="0 1 2 3"

UTILS="0.25 0.5 0.75 1.0"
ENERGY_TEST_UTILS="0.25 0.5 0.75 0.9 1.0"


RTES_DIR=/sys/rtes
PWR_FILE=$RTES_DIR/power

CPU_DIR=/sys/devices/system/cpu

do_check()
{
    local CHECK_FUNC=$1
    local DESC="$2"
    $CHECK_FUNC
    if [ "$?" -ne 0 ]
    then
        echo "Test '$CHECK_FUNC $DESC' failed"
        exit 1
    fi
}

# Parses: "0,4-7" into "0 4 5 6 7"
cpu_set_to_list()
{
    local CPU_SET=$1

    local CPU_SET_STR="$(echo $CPU_SET | sed 's/,/ /g')"
    for range in $CPU_SET_STR
    do
        if echo $range | grep -q '-'
        then
            echo -n "$range " | sed 's/-/ /g' | xargs -n 2 seq
        else
            echo -n "$range "
        fi
    done
    echo
}

list_len()
{
    local LIST="$@"
    local LEN=0
    for e in $LIST
    do
        local LEN=$(($LEN + 1))
    done
    echo $LEN
}

set_cpu_param()
{
    local SETTING=$1
    local VALUE=$2
    if [ "$#" -gt 2 ]
    then
        shift 2
        local CPUS="$@"
    else
        local CPUS="0 1 2 3"
    fi

    if [ $SETTING != "online" ]
    then
        local SETTING="cpufreq/$SETTING"
    fi

    for i in $CPUS
    do
        local FILE=$CPU_DIR/cpu$i/$SETTING
        if [ -e $FILE ]
        then
            echo $VALUE > $FILE
        fi
    done
}

set_freq()
{
    local FREQ=$1
    set_cpu_param scaling_setspeed $FREQ
    sleep 0.5
}

set_gov()
{
    local GOV=$1
    set_cpu_param scaling_governor $GOV
    sleep 0.5
}

check_value()
{
    local VAL=$1
    local REF=$2
    local DESC="$3"
    local TOLERANCE=0.10 # fraction: error/ref
    local CMP=$($TOOLS_DIR/fcalc $REF $VAL - a $REF s '/' $TOLERANCE '<')
    if [ "$CMP" -ne 0 ]
    then
        echo "FAILED: value not within tolerance (+-$($TOOLS_DIR/fcalc $TOLERANCE 100 '*')%): " \
            "value $VAL ref $REF"
        return 1
    else
        echo "value $VAL ref $REF (<+-$($TOOLS_DIR/fcalc $TOLERANCE 100 '*')%)"
    fi
    return 0
}

check_file()
{
    local FILE=$1
    if ! [ -e $RTES_DIR/$FILE ]
    then
        echo "FAILED: $FILE file not found"
        return 1
    fi
    return 0
}

launch_busyloop()
{
    local C=$1
    local T=$2
    local CPU=$3

    #chrt -f 50 $TOOLS_DIR/busyloop &
    $TOOLS_DIR/busyloop &
    BUSY_PID=$!
    sleep 0.5

    # 100% reserve: we want it to always be running
    ./reserve set $BUSY_PID $C $T $CPU

    chrt -f -p 50 $BUSY_PID &> /dev/null
}

launch_busyloop_util()
{
    local U=$1
    local CPU=$2

    local T=500
    local C=$($TOOLS_DIR/fcalc $U $T '*')

    launch_busyloop $C $T $CPU
}

launch_busyloops()
{
    local UTIL=$1
    shift
    local CPUS="$@"
    BUSY_PIDS=""
    for cpu in $CPUS
    do
        launch_busyloop_util $UTIL $cpu
        BUSY_PIDS="$BUSY_PIDS $BUSY_PID"
    done
}

kill_busyloops()
{
    #for pid in $BUSY_PIDS
    #do
    #    ./reserve cancel $pid
    #done
    kill -9 $BUSY_PIDS
    #killall busyloop
}

test_freq_tracking()
{
    local TEST_RESULT=0

    launch_busyloops 1.0 0

    for freq in $FREQS
    do
        set_freq $freq
        if ! check_file freq
        then
            TEST_RESULT=1
            break
        fi
        local REF_FREQ=$((freq / 1000))
        if ! grep "^[ \\t]*$REF_FREQ" $FREQ_FILE
        then
            local FREQ="$(head -1 $FREQ_FILE)"
            echo "FAILED: freq ($FREQ) does not match expected freq ($REF_FREQ)"
            TEST_RESULT=1
            break
        fi
    done

    kill_busyloops
    return $TEST_RESULT
}

calc_pwr()
{
    local FREQ=$1
    local NUM_CPUS=$2
    local KAPPA="0.00442"
    local ALPHA="1.67"
    local BETA="25.72"
    echo $($TOOLS_DIR/fcalc \
        1000 $FREQ '/' $ALPHA s p $KAPPA '*' $BETA '+' $NUM_CPUS '*')
}

check_pwr()
{
    local FREQ=$1
    local NUM_CPUS=$2
    check_value $(cat $PWR_FILE) $(calc_pwr $FREQ $NUM_CPUS) "power"
}

check_pwr_freqs()
{
    local NUM_CPUS=$1
    shift
    local FREQS="$@"
    local TEST_RESULT=0

    if ! check_file power
    then
        return 1 
    fi

    launch_busyloops 1.0 $(seq 0 $(($NUM_CPUS - 1)))

    for freq in $FREQS
    do
        echo "Check power consumption of $NUM_CPUS CPU(s) with freq $freq"
        set_freq $freq 
        if ! check_pwr $freq $NUM_CPUS
        then
            TEST_RESULT=1
            break
        fi
    done

    kill_busyloops
    return $TEST_RESULT
}

check_pwr_cpus()
{
    local FREQS="$@"
    for num_cpus in 2 3 4
    do
        if ! check_pwr_freqs $num_cpus $FREQS
        then
            return 1
        fi
    done
    return 0
}

test_pwr_one_freq_one_cpu()
{
    check_pwr_freqs 1 $MAX_FREQ
}

test_pwr_one_cpu_many_freqs()
{
    check_pwr_freqs 1 $FREQS
}

test_pwr_many_cpus()
{
    check_pwr_cpus $MAX_FREQ
}

test_pwr_many_cpus_many_freqs()
{
    check_pwr_cpus $FREQS
}

check_energy_freqs()
{
    local FREQS="$1"
    local UTILS="$2"

    for freq in $FREQS
    do
        set_freq $freq

        for util in $UTILS
        do
            local REF_PWR=$(calc_pwr $freq 1)
            local REF_ENERGY=$($TOOLS_DIR/fcalc $REF_PWR 10 '*' $util '*')
            echo 0 > $RTES_DIR/energy

            launch_busyloops $util 0
            sleep 10
            local PID=$(echo $BUSY_PIDS) # trim whitespace
            local ENERGY=$(cat $RTES_DIR/tasks/$PID/energy)
	    local TOTAL_ENERGY=$(cat $RTES_DIR/energy)
            kill_busyloops

	    echo Task util: $util, CPU freq: $freq 
            check_value $ENERGY $REF_ENERGY "energy"
            #check_value $TOTAL_ENERGY $REF_ENERGY "energy"
        done
    done
}

test_energy_one_freq_one_u()
{
    check_energy_freqs "$MAX_FREQ" "0.9"
}

test_energy_one_freq_one_u_max()
{
    check_energy_freqs "$MAX_FREQ" "1.0"
}

test_energy_many_freqs_one_u()
{
    check_energy_freqs "$FREQS" "0.9"
}

test_energy_many_freqs_one_u_max()
{
    check_energy_freqs "$FREQS" "1.0"
}

test_energy_max_freq_many_u()
{
    check_energy_freqs "$MAX_FREQ" "$UTILS"
}

test_energy_many_freqs_many_u()
{
    check_energy_freqs "$FREQS" "$UTILS"
}

reset_energy()
{
    echo 0 > $RTES_DIR/energy
}

check_energy_sum()
{
    local UTIL=$1
    shift
    local CPUS="$@"

    reset_energy

    local ENERGIES=""

    launch_busyloops $UTIL $CPUS
    sleep 10
    for pid in $BUSY_PIDS
    do
        local ENERGY=$(cat $RTES_DIR/tasks/$pid/energy)
        local ENERGIES="$ENERGIES $ENERGY"
    done
    kill_busyloops

    local TOTAL_E=0
    for e in $ENERGIES
    do
        local TOTAL_E=$($TOOLS_DIR/fcalc $TOTAL_E $e '+')
    done

    echo Per-task util: $UTIL, CPU list: $CPUS
    check_value $TOTAL_E $(cat $RTES_DIR/energy) "total energy"
}

test_energy_sum_two_same_cpu()
{
    check_energy_sum 0.25 0 0
}

test_energy_sum_three_same_cpu()
{
    check_energy_sum 0.25 0 0 0
}

test_energy_sum_two_diff_cpu()
{
    check_energy_sum 0.25 0 1
}

test_energy_sum_many()
{
    check_energy_sum 0.25 0 0 1 1 2 2 3 3
}

test_energymon_heavy()
{
    # MANUAL TEST: view the energymon (native and Android) output and check
    # that energy grows faster
    reset_energy
    set_freq $MAX_FREQ
    launch_busyloops 0.5 0
    sleep 0.5
    ./energymon $BUSY_PIDS
    kill_busyloops
}

test_energymon_light()
{
    # MANUAL TEST: view the energymon (native and Android) output and check that
    # energy grows slower
    reset_energy
    set_freq 640000 
    launch_busyloops 0.25 0
    sleep 0.5
    ./energymon $BUSY_PIDS
    kill_busyloops
}

test_energymon_heavy2()
{
    # MANUAL TEST: view the energymon (native and Android) output and check
    # that energy grows same as in heavy test
    reset_energy
    set_freq $MAX_FREQ
    launch_busyloops 0.25 0 0
    sleep 0.5
    ./energymon 0
    kill_busyloops
}

cycle_freqs()
{
    for freq in $FREQS
    do
        sleep 5
        set_freq $freq
    done
}

test_energymon_freqs_android_app()
{
    # MANUAL TEST: view energymon (native and Android) output and check that the
    # frequency changes
    reset_energy
    launch_busyloops 0.25 0
    cycle_freqs &
    ./energymon 0
}

add_tasks()
{
    NUM_TASKS=3 # one per CPU
    for i in $(seq $NUM_TASKS)
    do
        sleep 5
        launch_busyloops 0.25 $i
    done
    kill_busyloops
}

test_energymon_tasks()
{
    # MANUAL TEST: view energymon (native and Android) output and check that
    # energy grows progressively faster (as new tasks are added)
    reset_energy
    launch_busyloops 0.25 0
    add_tasks &
    ./energymon 0
    kill_busyloops
}


check_online_count()
{
    local REF_COUNT=$1

    local ONLINE=$(cat $CPU_DIR/online)
    local ONLINE_LIST=$(cpu_set_to_list $ONLINE)
    local ONLINE_COUNT=$(list_len $ONLINE_LIST)

    if [ "$ONLINE_COUNT" -ne "$REF_COUNT" ]
    then
        echo "FAILED: online processors ($ONLINE) " \
            "do not match expected ($REF_COUNT)"
        return 1
    fi
    return 0
}

check_unused()
{
    local UTIL=$1
    local CPUS="$2"
    local REF_COUNT="$3"

    local TEST_RESULT=0
    launch_busyloops $UTIL $CPUS
    sleep 2
    if ! check_online_count $REF_COUNT
    then
        TEST_RESULT=1
    fi
    kill_busyloops
    return $TEST_RESULT
}

test_unused_static()
{
    check_unused 0.25 0 1
}

test_unused_partition()
{
    check_unused 0.75 "-1 -1 -1" 3
}

test_unused_partition_wfd()
{
    echo WFD > $RTES_DIR/partition_policy
    check_unused 0.25 "-1 -1 -1" 1
}

test_unused_partition_lst()
{
    echo LST > $RTES_DIR/partition_policy
    check_unused 0.25 "-1 -1 -1" 3
}

test_partition_lst()
{
    local TEST_RESULT=0
    echo LST > $RTES_DIR/partition_policy
    launch_busyloops 0.25 -1 -1 -1 -1 -1 -1 -1 -1
    local REF_COUNT=2
    cp $RTES_DIR/reserves reserves.$$
    for core in 0 1 2 3
    do
        local COUNT=$(grep -cE "[^0-9]+$core[^0-9]+" $RTES_DIR/reserves)
        if [ "$COUNT" -ne $REF_COUNT ]
        then
            echo "FAILED: task count on core $core ($COUNT) " \
                "does not match expected ($REF_COUNT)"
            TEST_RESULT=1
            break
        fi
    done
    kill_busyloops
    return $TEST_RESULT
}

check_freq()
{
    local FREQ=$1
    local REF_FREQ=$2
    #if [[ ("$FREQ" -eq "$REF_FREQ") || ("$FREQ" -eq "1300" && "$REF_FREQ" -eq "1200") ]]
    if [[ "$FREQ" -eq "$REF_FREQ" ]] 
    then
        echo "freq ($FREQ) matches expected ($REF_FREQ)"
	    return 0	
    fi
    if [[ "$FREQ" -eq "1300" && "$REF_FREQ" -eq "1200" ]]
    then
        echo "freq ($FREQ) matches expected ($REF_FREQ)"
	    return 0	
    fi
    echo "FAILED: freq ($FREQ) does not match expected ($REF_FREQ)"
    return 1
}

test_sysclock_min()
{
    set_gov sysclock
    local BUSY_PIDS=""
    launch_busyloop 200 1000 0
    local BUSY_PIDS="$BUSY_PIDS $BUSY_PID"
    echo "Tasks (C, T): (200, 1000)"

    local REF_FREQ=340 # 1200*0.20=240
    local FREQ=$(head -1 $RTES_DIR/freq)
    for pid in $BUSY_PIDS
    do
        ./reserve cancel $pid
    done
    kill $BUSY_PIDS
    #killall busyloop
    check_freq $FREQ $REF_FREQ
}

test_sysclock_max()
{
    set_gov sysclock
    local BUSY_PIDS=""
    launch_busyloop 475 500 0
    local BUSY_PIDS="$BUSY_PIDS $BUSY_PID"
    echo "Tasks (C, T): (475, 500)"

    local REF_FREQ=1200 # 1200*0.95=1140
    local FREQ=$(head -1 $RTES_DIR/freq)
    for pid in $BUSY_PIDS
    do
        ./reserve cancel $pid
    done
    kill $BUSY_PIDS
    #killall busyloop
    check_freq $FREQ $REF_FREQ
}

test_sysclock_one_cpu_1()
{
    set_gov sysclock
    local BUSY_PIDS=""
    launch_busyloop 300 1000 0
    local BUSY_PIDS="$BUSY_PIDS $BUSY_PID"
    launch_busyloop 200 2000 0
    local BUSY_PIDS="$BUSY_PIDS $BUSY_PID"
    launch_busyloop 250 4000 0
    local BUSY_PIDS="$BUSY_PIDS $BUSY_PID"

    echo "Tasks (C, T): (300, 1000), (200, 2000), (250, 4000)"

    local REF_FREQ=640 # 1200*0.4625=555
    local FREQ=$(head -1 $RTES_DIR/freq)
    for pid in $BUSY_PIDS
    do
        ./reserve cancel $pid
    done
    kill $BUSY_PIDS
    #killall busyloop
    check_freq $FREQ $REF_FREQ
}

test_sysclock_one_cpu_2()
{
    set_gov sysclock
    local BUSY_PIDS=""
    launch_busyloop 200 600 0
    local BUSY_PIDS="$BUSY_PIDS $BUSY_PID"
    launch_busyloop 150 700 0
    local BUSY_PIDS="$BUSY_PIDS $BUSY_PID"
    launch_busyloop 100 800 0
    local BUSY_PIDS="$BUSY_PIDS $BUSY_PID"

    echo "Tasks (C, T): (200, 600), (150, 700), (100, 800)"

    local REF_FREQ=1000 # 1200000*0.75=900000
    local FREQ=$(head -1 $RTES_DIR/freq)
    for pid in $BUSY_PIDS
    do
        ./reserve cancel $pid
    done
    kill $BUSY_PIDS
    #killall busyloop
    check_freq $FREQ $REF_FREQ
}

test_sysclock_one_cpu_3()
{
    set_gov sysclock
    local BUSY_PIDS=""
    launch_busyloop 200 1375 0
    local BUSY_PIDS="$BUSY_PIDS $BUSY_PID"
    launch_busyloop 800 1600 0
    local BUSY_PIDS="$BUSY_PIDS $BUSY_PID"
    launch_busyloop 250 7000 0
    local BUSY_PIDS="$BUSY_PIDS $BUSY_PID"

    echo "Tasks (C, T): (200, 1375), (800, 1600), (250, 7000)"

    local REF_FREQ=1000 # 1200*0.7273=873
    # But if task 3's limit is considered then 1200*0.6953=834 => 760
    local FREQ=$(head -1 $RTES_DIR/freq)
    for pid in $BUSY_PIDS
    do
        ./reserve cancel $pid
    done
    kill $BUSY_PIDS
    #killall busyloop
    check_freq $FREQ $REF_FREQ
}

test_sysclock_two_cpu_lst()
{
    set_gov sysclock
    echo LST > $RTES_DIR/partition_policy

    local BUSY_PIDS=""
    launch_busyloop 300 1000 -1
    local BUSY_PIDS="$BUSY_PIDS $BUSY_PID"
    launch_busyloop 200 2000 -1
    local BUSY_PIDS="$BUSY_PIDS $BUSY_PID"
    launch_busyloop 250 4000 -1
    local BUSY_PIDS="$BUSY_PIDS $BUSY_PID"

    launch_busyloop 200 600 -1
    local BUSY_PIDS="$BUSY_PIDS $BUSY_PID"
    launch_busyloop 150 700 -1
    local BUSY_PIDS="$BUSY_PIDS $BUSY_PID"
    launch_busyloop 100 800 -1
    local BUSY_PIDS="$BUSY_PIDS $BUSY_PID"

    echo "Policy: $(cat $RTES_DIR/partition_policy)"
    echo "Tasks (C, T): (300, 1000), (200, 2000), (250, 4000), (200, 600), (150, 700), (100, 800)"

    local REF_FREQ=475 # (250,4000 + 150,700: 0.2857 * 1200 => 343
    local FREQ=$(head -1 $RTES_DIR/freq)
    for pid in $BUSY_PIDS
    do
        ./reserve cancel $pid
    done
    kill $BUSY_PIDS
    #killall busyloop
    check_freq $FREQ $REF_FREQ
}

test_nop()
{
    return 0
}


do_test()
{
    local TEST_NAME=$1
    test_$TEST_NAME
    if [ "$?" -ne 0 ]
    then
        echo "Test $TEST_NAME failed"
        exit 1
    fi
}

check_suspension()
{
    local COUNT_PID=$1
    local COUNT_FILE=$2

    if ! [ -r $COUNT_FILE ]
    then
        echo "FAILED: counter file not found: $COUNT_FILE"
        return 1
    fi

    COUNT_1=$(cat $COUNT_FILE)
    sleep 1
    COUNT_2=$(cat $COUNT_FILE)
    sleep 5
    COUNT_3=$(cat $COUNT_FILE)
    if [ $COUNT_2 -ne $COUNT_1 ]
    then
        echo "FAILED: task was not suspended ($COUNT_1, $COUNT_2)"
        return 1
    fi
    if [ $COUNT_3 -eq $COUNT_1 ]
    then
        echo "FAILED: task did not resume ($COUNT_2, $COUNT_3)"
        return 1
    fi
    return 0
}

#UNUSED IN FALL 2017 TEST SUITE
test_enforce()
{
    local TEST_RESULT=0
    local COUNT_FILE=countfile.$$
    chrt -f 50 $TOOLS_DIR/count sleep 10000000 0 $COUNT_FILE &
    COUNT_PID=$!
    sleep 0.5
    ./reserve set $COUNT_PID 10 5000 0
    sleep 0.5
    if ! check_suspension $COUNT_PID $COUNT_FILE
    then
        TEST_RESULT=1
    fi
    kill $COUNT_PID
    return $TEST_RESULT
}

check_util_data()
{
    local PERIODIC_PID=$1
    local NUM_LINES_REF=$2
    local UTIL_REF=$3

    local UTIL_FILE=$TASKMON_PATH/util/$PERIODIC_PID
    if ! [ -e "$UTIL_FILE" ]
    then
        echo "FAILED: util file not found: '$UTIL_FILE'" 1>&2
        return 1
    fi

    mkdir -p tmp
    local UTIL_TMP_FILE=tmp/$$-util-$PERIODIC_PID
    cat $UTIL_FILE > $UTIL_TMP_FILE
    local NUM_LINES=$(wc -l $UTIL_TMP_FILE | cut -d' ' -f1)
    if [ "$NUM_LINES" -lt $NUM_LINES_REF ]
    then
        echo "FAILED: too few data points: $NUM_LINES" 1>&2
        return 1
    fi

    local LINE=0
    while read p
    do
        LINE=$((LINE+1))
        if [ "$LINE" -eq 1 ]
        then
            continue
        fi
        TOLERANCE=0.05
        UTIL="$(echo $p | cut -d' ' -f2)" 
        RESULT=$($TOOLS_DIR/fcalc $UTIL $UTIL_REF - 'a' $TOLERANCE '<')
        if [ "$RESULT" -ne 0 ]
        then
            echo "FAILED: utilization not within $TOLERANCE of $UTIL_REF: $UTIL" 1>&2
            return 1
        fi
    done < $UTIL_TMP_FILE
    rm $UTIL_TMP_FILE
    return 0
}


test_enforce_2()
{
    local UTIL_REF=0.1 # change this if changing the following
    chrt -f 50 $TOOLS_DIR/busyloop &
    local PERIODIC_PID=$!
    sleep 1
    ./reserve set $PERIODIC_PID 100 1000 0
    echo 1 > $ENABLE_TASKMON_FILE
    sleep 6
    echo 0 > $ENABLE_TASKMON_FILE
    sleep 2
    NUM_LINES_REF=5
    if ! check_util_data $PERIODIC_PID $NUM_LINES_REF $UTIL_REF
    then
        return 1
    fi
    ./reserve cancel $PERIODIC_PID
    sleep 1
    if [ -e "$UTIL_FILE" ]
    then
        echo "FAILED: util file still exists: '$UTIL_FILE'" 1>&2
        return 1
    fi
    kill $PERIODIC_PID
    return 0
}

#Unused in Fall 2017 Test Suite
test_end_job()
{
    local TEST_RESULT=0
    COUNT_FILE=countfile.$$
    chrt -f 50 $TOOLS_DIR/count reserve 10000000 5000 $COUNT_FILE &
    COUNT_PID=$!
    sleep 0.5
    if ! check_suspension $COUNT_PID $COUNT_FILE
    then
        TEST_RESULT=1
    fi
    if [ "$CANCEL_RESERVE" = 1 ]
    then
        ./reserve cancel $COUNT_PID
    fi
    kill $COUNT_PID
    return $TEST_RESULT
}

#Unused in Fall 2017 Test Suite
test_enforce_no_signal()
{
    local TEST_RESULT=0
    local COUNT_FILE=countfile.$$
    chrt -f 50 $TOOLS_DIR/count sleep 10000000 0 $COUNT_FILE &
    COUNT_PID=$!
    ./reserve set $COUNT_PID 10 5000 0
    sleep 1

    kill -SIGUSR2 $COUNT_PID
    sleep 0.5

    #if ! ps | grep -q $COUNT_PID
    if ! [ -e /proc/$COUNT_PID ]
    then
        echo "FAILED: task handled signal"
        TEST_RESULT=1
    fi

    if [ "$CANCEL_RESERVE" = 1 ]
    then
        ./reserve cancel $COUNT_PID
    fi
    kill $COUNT_PID
    return $TEST_RESULT
}

check_easyperiodic_util()
{
    local C=$1
    local T=$2
    local TEST_RESULT=0
    local UTIL_REF=$($TOOLS_DIR/fcalc $T $C '/' )
    ./easyperiodic $C $T 0 &
    local PERIODIC_PID=$!
    sleep 0.5
    ./reserve set $PERIODIC_PID $(($C+20)) $T 0
    echo 1 > $ENABLE_TASKMON_FILE
    sleep 4
    local UTIL_FILE=/sys/rtes/taskmon/util/$PERIODIC_PID
    if ! [ -e $UTIL_FILE ]
    then
        echo "FAILED: utilization file not found ($UTIL_FILE)"
        echo 0 > $ENABLE_TASKMON_FILE
        kill $PERIODIC_PID
        return 1
    fi
    local UTIL_SNAPSHOT=util.$PERIODIC_PID
    cp $UTIL_FILE $UTIL_SNAPSHOT
    echo 0 > $ENABLE_TASKMON_FILE

    local LINE=0
    while read p
    do
        LINE=$((LINE+1))
        if [ "$LINE" -eq 1 ]
        then
            continue
        fi
        local TOLERANCE=0.10
        local UTIL="$(echo $p | cut -d' ' -f2)" 
        local RESULT=$($TOOLS_DIR/fcalc $UTIL $UTIL_REF - 'a' $TOLERANCE '<')
        if [ "$RESULT" -ne 0 ]
        then
            echo "FAILED: utilization not within $TOLERANCE of $UTIL_REF: $UTIL" 1>&2
            TEST_RESULT=1
            break
        fi
    done < $UTIL_SNAPSHOT

    if [ "$TEST_RESULT" -eq 0 ]
    then
        if [ "$LINE" -lt 1 ]
        then
            echo "FAILED: no data points in utilization file"
            return 1
        fi
    fi

    kill $PERIODIC_PID
    return $TEST_RESULT
}

test_easyperiodic_util_light()
{
    check_easyperiodic_util 100 500
}
test_easyperiodic_util_med()
{
    check_easyperiodic_util 200 500
}
test_easyperiodic_util_heavy()
{
    check_easyperiodic_util 450 500
}

test_reserve_status()
{
    local PRIO_BASE=50

    local BUSY_PIDS=""
    for i in 0 1 2 3
    do
        local CPU=$i
        local PRIO=$(($PRIO_BASE+$i))
        chrt -f $PRIO $TOOLS_DIR/busyloop &
        local BUSY_PID=$!
        BUSY_PIDS="$BUSY_PIDS $BUSY_PID"
        ./reserve set $BUSY_PID 50 500 $CPU
    done

    local STATUS_SNAPSHOT=reserves.$$
    cp $STATUS_FILE $STATUS_SNAPSHOT
    cat $STATUS_SNAPSHOT

    local TEST_RESULT=0
    local i=0
    for pid in $BUSY_PIDS
    do
        local CPU=$i
        local PRIO=$(($PRIO_BASE + $i))

        local FOUND=0
        while read p
        do
            if echo $p | grep -q $pid
            then
                FOUND=1
                break
            fi
        done < $STATUS_SNAPSHOT

        if [ $FOUND -eq 0 ]
        then
            echo "FAILED: task PID not found in status file ($STATUS_FILE)"
            TEST_RESULT=1
            break
        fi

        if ! echo $p | grep -q busyloop
        then
            echo "FAILED: task name not found in status line ($STATUS_FILE)"
            TEST_RESULT=1
            break
        fi

        # The priority p shows up as (p-1): need to investigate whether that is
        # expected, and if so, then change this test to test for (p-1)
        #if ! echo $p | grep -q -E "[ \\t]+$PRIO[ \\t]+"
        #then
            #echo "FAILED: task priority not found in status line"
            #TEST_RESULT=1
            #break
        #fi

        if ! echo $p | grep -q -E "[ \\t]+$CPU[ \\t]+"
        then
            echo "FAILED: task cpu not found in status line ($STATUS_FILE)"
            TEST_RESULT=1
            break
        fi

        i=$(($i + 1))
    done

    if [ "$CANCEL_RESERVE" = 1 ]
    then
        for pid in $BUSY_PIDS
        do
            ./reserve cancel $pid
        done
    fi
    kill $BUSY_PIDS
    return $TEST_RESULT
}

check_reserves()
{
    local PIDS="$*"
    cp $STATUS_FILE status.$$
    for PID in $PIDS
    do
        local FOUND=0
        while read p
        do
            if echo $p | grep -q $PID
            then
                FOUND=1
                break
            fi
        done < $STATUS_FILE

        if [ $FOUND -ne 1 ]
        then
            return 1
        fi
    done
    return 0
}

launch_reserved_sleep()
{
    local ARGS="$1"
    local CPU=$(echo $ARGS | cut -d'_' -f1)
    local C=$(echo $ARGS | cut -d'_' -f2)
    local T=$(echo $ARGS | cut -d'_' -f3)
    chrt -f 50 $TOOLS_DIR/sleeploop $C $T &
    BUSY_PID=$!
    BUSY_PIDS="$BUSY_PIDS $BUSY_PID"
    ./reserve set $BUSY_PID $C $T $CPU
}

check_taskset()
{
    local TASKSET="$*"
    echo TASKSET="$TASKSET"
    local TEST_RESULT=0
    BUSY_PIDS=""
    for task in $TASKSET
    do
        launch_reserved_sleep "$task"
    done
    sleep 2
    ps | grep sleeploop > status_ps.$$
    if ! check_reserves $BUSY_PIDS
    then
        echo "FAILED: expected reserves not found in status"
        TEST_RESULT=1
    fi
    if [ "$CANCEL_RESERVE" = 1 ]
    then
        for pid in $BUSY_PIDS
        do
            ./reserve cancel $pid
            sleep 0.5
        done
    fi
    kill $BUSY_PIDS
    sleep 1
    return $TEST_RESULT
}

test_nop()
{
    return 0
}

accept_taskset()
{
    check_taskset $*
}

reject_taskset()
{
    if check_taskset $*
    then
        return 1
    fi
    return 0
}

test_adm_single_cpu_accept_1()
{
    accept_taskset \
        "0_250_500"
}

test_adm_single_cpu_accept_u_2()
{
    accept_taskset \
        "0_200_500" \
        "0_100_300"
}

test_adm_single_cpu_reject_2()
{
    reject_taskset \
        "0_250_500" \
        "0_300_500"
}

test_adm_single_cpu_accept_u_3()
{
    accept_taskset \
        "0_200_500" \
        "0_150_600" \
        "0_40_400"
}

test_adm_single_cpu_accept_u_5()
{
    accept_taskset \
        "0_20_600" \
        "0_60_500" \
        "0_100_450" \
        "0_30_300" \
        "0_50_200"
}

test_adm_single_cpu_accept_h_2()
{
    accept_taskset \
        "0_250_500" \
        "0_125_250"
}

test_adm_single_cpu_accept_h_3()
{
    accept_taskset \
        "0_200_400" \
        "0_50_200" \
        "0_25_100"
}

test_adm_single_cpu_accept_h_10()
{
    accept_taskset \
        "0_200_2000" \
        "0_100_1000" \
        "0_100_1000" \
        "0_50_500" \
        "0_25_250" \
        "0_200_2000" \
        "0_100_1000" \
        "0_100_1000" \
        "0_50_500" \
        "0_25_250"
}

test_adm_single_cpu_accept_r_2()
{
    accept_taskset \
        "0_20_50" \
        "0_40_80" 
}

test_adm_single_cpu_accept_r_3()
{
    accept_taskset \
        "0_10_40" \
        "0_20_60" \
        "0_25_100" 
}

PARTITION_POLICY_FILE=/sys/rtes/partition_policy

test_adm_bin_packing_nfd_success_1()
{
    echo NF > $PARTITION_POLICY_FILE
    accept_taskset \
        "-1_40_50" \
        "-1_50_100" \
        "-1_140_200" \
        "-1_60_100" \
        "-1_40_200" \
        "-1_20_50" \
        "-1_30_300" 
}

test_adm_bin_packing_nfd_fail_1()
{
    echo NF > $PARTITION_POLICY_FILE
    reject_taskset \
        "-1_40_50" \
        "-1_50_100" \
        "-1_140_200" \
        "-1_60_100" \
        "-1_40_200" \
        "-1_20_50" \
        "-1_30_300" \
        "-1_410_1000"
}

test_adm_bin_packing_bfd_success_1()
{
    echo BF > $PARTITION_POLICY_FILE
    accept_taskset \
        "-1_40_50" \
        "-1_50_100" \
        "-1_140_200" \
        "-1_60_100" \
        "-1_40_200" \
        "-1_20_50" \
        "-1_30_300" \
	    "-1_80_200"
}

test_adm_bin_packing_bfd_fail_1()
{
    echo BF > $PARTITION_POLICY_FILE
    reject_taskset \
        "-1_40_50" \
        "-1_50_100" \
        "-1_140_200" \
        "-1_60_100" \
        "-1_40_200" \
        "-1_20_50" \
        "-1_30_300" \
	    "-1_80_200" \
	    "-1_90_300"
}

test_adm_bin_packing_wfd_success_1()
{
    echo WF > $PARTITION_POLICY_FILE
    accept_taskset \
        "-1_40_50" \
        "-1_50_100" \
        "-1_140_200" \
        "-1_60_100" \
        "-1_40_200" \
        "-1_20_50" \
        "-1_40_400" \
        "-1_20_100" 
}

test_adm_bin_packing_wfd_fail_1()
{
    echo WF > $PARTITION_POLICY_FILE
    reject_taskset \
        "-1_40_50" \
        "-1_50_100" \
        "-1_140_200" \
        "-1_60_100" \
        "-1_40_200" \
        "-1_20_50" \
        "-1_40_400" \
	    "-1_40_100" 
}

test_adm_bin_packing_ffd_success_1()
{
    echo FF > $PARTITION_POLICY_FILE
    accept_taskset \
        "-1_40_50" \
        "-1_50_100" \
        "-1_140_200" \
        "-1_60_100" \
        "-1_20_50" \
        "-1_40_400" \
        "-1_40_200" \
        "-1_20_100" 
}

test_adm_bin_packing_ffd_fail_1()
{
    echo FF > $PARTITION_POLICY_FILE
    reject_taskset \
        "-1_40_50" \
        "-1_50_100" \
        "-1_140_200" \
        "-1_60_100" \
        "-1_20_50" \
        "-1_40_400" \
        "-1_40_200" \
        "-1_20_100" \
	"-1_30_100" 
}

partition_bonus()
{
    accept_taskset \
        "-1_600_1000" \
        "-1_1200_2000" \
        "-1_200_2000" \
        "-1_200_2000" \
        "-1_400_1000"
}

test_adm_bin_packing_bonus()
{
    echo PA > $PARTITION_POLICY_FILE
    partition_bonus
}

test_adm_bin_packing_baseline()
{
    echo WF > $PARTITION_POLICY_FILE
    partition_bonus
}

test_adm_single_cpu()
{
    do_test adm_single_cpu_accept_1
    do_test adm_single_cpu_accept_u_2
    do_test adm_single_cpu_reject_2
    do_test adm_single_cpu_accept_u_3
    do_test adm_single_cpu_accept_u_5
    do_test adm_single_cpu_accept_h_2
    do_test adm_single_cpu_accept_h_3
    do_test adm_single_cpu_accept_h_10
    do_test adm_single_cpu_accept_r_2
    do_test adm_single_cpu_accept_r_3
}

test_adm_multi_cpu()
{
    do_test adm_bin_packing_nfd_success_1
    do_test adm_bin_packing_nfd_fail_1
    do_test adm_bin_packing_bfd_success_1
    do_test adm_bin_packing_bfd_fail_1
    do_test adm_bin_packing_wfd_success_1
    do_test adm_bin_packing_wfd_fail_1
    do_test adm_bin_packing_ffd_success_1
    do_test adm_bin_packing_ffd_fail_1
}

test_help()
{
    echo "all"
    #echo "enforce_2"
    #echo "enforce_no_signal"
    #echo "end_job"
    echo "easyperiodic_util_light"
    echo "easyperiodic_util_med"
    echo "easyperiodic_util_heavy"
    echo
    echo "adm_single_cpu"
    echo
    echo "adm_single_cpu_accept_1"
    echo "adm_single_cpu_accept_u_2"
    echo "adm_single_cpu_reject_2"
    echo "adm_single_cpu_accept_u_3"
    echo "adm_single_cpu_accept_u_5"
    echo "adm_single_cpu_accept_h_2"
    echo "adm_single_cpu_accept_h_3"
    echo "adm_single_cpu_accept_h_10"
    echo "adm_single_cpu_accept_r_2"
    echo "adm_single_cpu_accept_r_3"
    echo
    echo "adm_multi_cpu"
    echo
    echo "adm_bin_packing_nfd_success_1"
    echo "adm_bin_packing_nfd_fail_1"
    echo "adm_bin_packing_bfd_success_1"
    echo "adm_bin_packing_bfd_fail_1"
    echo "adm_bin_packing_wfd_success_1"
    echo "adm_bin_packing_wfd_fail_1"
    echo "adm_bin_packing_ffd_success_1"
    echo "adm_bin_packing_ffd_fail_1"

    echo "partition_lst : requires /sys/rtes/reserves"

    echo "energy_max_freq_many_u"
    
    echo "energy_sum_two_diff_cpu"
    echo "energy_sum_many"

    echo "energymon_heavy"
    echo "energymon_light"
}

if [ "$#" -lt 1 -o "$1" = "help" ]
then
    test_help
    exit 0
fi

# System setup
echo 0 > /sys/module/cpu_tegra3/parameters/auto_hotplug
for i in 0 1 2 3
do
    CPU_PATH=/sys/devices/system/cpu/cpu$i
    if [ "$(cat $CPU_PATH/online)" -ne 1 ]
    then
        echo 1 > $CPU_PATH/online
    fi
    sleep 0.5
    echo performance > $CPU_PATH/cpufreq/scaling_governor
done

echo 1 > $RTES_DIR/config/energy

sleep 0.5

TEST=$1
shift
test_$TEST $@
if [ "$?" -ne 0 ]
then
    echo "Test $TEST failed"
    exit 1
fi
echo "Test $TEST passed"
exit 0
