#!/usr/bin/sh

init () {
	modprobe visp
	if [ $? -ne 0 ]; then
		echo "modprobe error"
		exit 1
	fi

	dmesg -C
}

deinit () {
	rmmod visp
	if [ $? -ne 0 ]; then
		echo "rmmod error"
		exit 1
	fi
}

check_kmemleak () {
	if [ -f /sys/kernel/debug/kmemleak ]; then
		echo "****************  KMEMLEAK  ****************"
		echo scan > /sys/kernel/debug/kmemleak
		cat /sys/kernel/debug/kmemleak
	fi
}

run_visptest () {
	local arg0=$1
	local arg1=$2

	echo "=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-="

	./visptest $arg0 $arg1

	dmesg | grep -qz "BUG: " && exit 1
	dmesg | grep -qz "WARNING: " && exit 1
}

init

# a normal exec
run_visptest

# faulty runs
run_visptest --exit_at test_add_valid_operations
run_visptest --exit_at test_add_valid_rw_operations
run_visptest --exit_at test_dma_fence

# a normal exec again
run_visptest

check_kmemleak

deinit
