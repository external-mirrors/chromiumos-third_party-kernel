#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1
# Copyright (C) 2022 Google LLC

BASE=/sys/kernel/debug/fail_function

add_null_fail () {
	func=$1

	echo "Configuring ${func} error injection to return NULL"

	echo ${func} >> ${BASE}/inject
	if [ $? -ne 0 ]; then
		echo "Unable to setup ${func} fail injection"
		exit 1
	fi

	# This is for functions that return integer vals
	# Need to think how to handle it
	# printf %#x -12 > ${BASE}/$func/retval
	echo N > ${BASE}/task-filter
	echo 90 > ${BASE}/probability
	echo 0 > ${BASE}/interval
	printf %#x -1 > ${BASE}/times
	echo 0 > ${BASE}/space
	echo 1 > ${BASE}/verbose

	echo "Done"
}

add_errno_fail () {
	func=$1

	echo "Configuring ${func} error injection to return ERRNO"

	echo ${func} >> ${BASE}/inject
	if [ $? -ne 0 ]; then
		echo "Unable to setup ${func} fail injection"
		exit 1
	fi

	printf %#x -12 > ${BASE}/$func/retval
	echo N > ${BASE}/task-filter
	echo 90 > ${BASE}/probability
	echo 0 > ${BASE}/interval
	printf %#x -1 > ${BASE}/times
	echo 0 > ${BASE}/space
	echo 1 > ${BASE}/verbose

	echo "Done"
}

add_fail_function () {
	while [ $# -ne 0 ]; do
		func=$1
		shift

		tp=`cat ${BASE}/injectable | grep ${func} | grep -c NULL`
		if [ ${tp} -eq 1 ]; then
			add_null_fail $func
			continue
		fi

		tp=`cat ${BASE}/injectable | grep ${func} | grep -c ERRNO`
		if [ ${tp} -eq 1 ]; then
			add_errno_fail $func
			continue
		fi

		echo "Unknown fail function: ${func}"
		exit 1
	done
}

clear_fail_function () {
	echo "Removing fail function configuration"

	echo '' > ${BASE}/inject
	echo 0 > ${BASE}/probability
}

check_config () {
	if [ ! -f ${BASE}/inject ]; then
		echo "Error injection appears to be disabled"
		echo "Make sure to enable CONFIG_FUNCTION_ERROR_INJECTION"
		exit 1
	fi
}

print_help () {
	echo "Usage:"
	echo "$0 add <func> [<func>...]"
	echo "$0 clear"
	echo "------------------------------------------"
	echo "Supported functions:"
	cat ${BASE}/injectable | grep cam_

	echo "------------------------------------------"
	echo "Current fail injection list"
	cat ${BASE}/inject
	exit 0
}

main () {
	check_config

	case "g$1" in
		"g")
			print_help
			;;
		"gadd")
			shift
			add_fail_function "$@"
			;;
		"gclear")
			clear_fail_function
			;;
	esac
}

main "$@"
