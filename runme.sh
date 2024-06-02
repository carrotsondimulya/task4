#!/bin/bash
touch data
for i in {1..500}
do
echo "${i}" >> data
done
for i in {1..500}
do
echo "-${i}" >> data
done
echo "0" > zero
mkdir /tmp/clients_log
make > /dev/null

echo -n my_socket > /tmp/config
./main -s my_socket /tmp/my_socket.log &
SERVER_PID=$!
echo "start clients..."
function start_clients {    # start_clients(count, delay)
    declare -A CLIENTS_PIDS
    for i in {1..$1}
    do
        ./main -c /tmp/config $2 < data &> /tmp/clients_log/client_${i}.log &
        CLIENTS_PIDS[$i]=$!
    done
    for pid in ${CLIENTS_PIDS[*]}
    do
        wait -f ${pid}
    done
}

echo "------------------result test 1 and test 2------------------" > result.txt
for count in {1..10}
do
    echo "INFO: Test 2: count=${count}"
    start_clients 100 0.1
    rm -f /tmp/clients_log/*
done

echo "INFO: Test 2: clients close"
grep "Используется файловый дескриптор" /tmp/my_socket.log | head -n 1 >> result.txt
grep "Используется файловый дескриптор" /tmp/my_socket.log | tail -n 1 >> result.txt
grep "Указатель на границу кучи" /tmp/my_socket.log | head -n 1 >> result.txt
grep "Указатель на границу кучи" /tmp/my_socket.log | tail -n 1 >> result.txt
echo >> result.txt
./main -c /tmp/config 0 < zero &>> result.txt
kill -SIGINT $SERVER_PID

echo "ending test 2 success!"

echo "------------------test 3------------------" >> result.txt
rm -f /tmp/my_socket.log /tmp/my_socket
./main -s my_socket /tmp/my_socket.log &
SERVER_PID=$!
echo "running test 3..."

echo "number of clients: 5,25,50,75,100 with delays [0, 1] step:0,2" 
clients=(5 25 50 75 100)
delays=(0 0.2 0.4 0.6 0.8 1)

printf "%-10s | %-10s | %-15s | %-15s | %-15s\n" "Клиентов" "Задержка" "Сум-ая задержка" "Работа сервера" "Эффективность" >> result.txt

for count in ${clients[*]}
do
    for delay in ${delays[*]}
    do
        start_time=$(date -u +%s)
        start_clients ${count} ${delay}
        end_time=$(date -u +%s)
        clients_work=$(($end_time-$start_time))
        long_delay=$(grep "Sum of delay" /tmp/clients_log/* | awk '{print $5}' | sort -rn | head -n 1)
        result=$(bc <<< $clients_work-$long_delay)
        printf "%-10s | %-10s | %-15s | %-15s | %-15s\n" "${count}" "${delay}" "${long_delay}" "${clients_work}" "${result}" >> result.txt
        rm -f /tmp/clients_log/*
    done
done
echo >> result.txt
./main -c /tmp/config 0 < zero &>> result.txt
kill -SIGINT $SERVER_PID

rm -f /tmp/config /tmp/my_socket.log /tmp/my_socket data zero ./main
rm -rf /tmp/clients_log

echo "Success running all tests! See result.txt"