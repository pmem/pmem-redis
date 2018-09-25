#!/bin/bash
source env.sh

count=0
while true
do
    ./gen_test_case 1024 8192 1000000 20 > /tmp/jemallocat.testcase
    if [ $? -eq 0 ]; then
        ./do_test_case 1024 < /tmp/jemallocat.testcase
        if [ $? -ne 0 ]; then
            echo "test case not passed!"
            exit 1
        fi
        count=$((${count} + 1))
        echo "${count} tests done"
    fi
done
