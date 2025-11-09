#!/bin/sh
#Benchmark comparison of the custom diff implementation and GNU diff on large files with generated test inputs
#Copyright (C) 2025 Ivan Gaydardzhiev
#Licensed under GPL-3.0-only

base="file1.txt"
mod="file2.txt"
lines=512

fprand() {
	tr -dc 'A-Za-z0-9' </dev/urandom | head -c 20
}

seq 1 $lines | while read n; do
	echo "Line $n - $(fprand)"
done > "$base"

cp "$base" "$mod"
awk 'NR % 2 == 0 {print $0 " - MODIFIED"} NR % 2 == 1 {print $0}' "$base" > "$mod"

flogos() {
	sed -n '2s/^.\(.*\)/\1/p' "$0"
	sed -n '3s/^.\(.*\)/\1/p' "$0"
	sed -n '4s/^.\(.*\)/\1/p' "$0"
	printf "\n"
}

ftime() {
	c=$1
	f1=$2
	f2=$3
	s=$(date +%s%N)
	eval "$c '$f1' '$f2'" >/dev/null 2>&1
	d=$(date +%s%N)
	e=$((d - s))
	printf "%s execution time: %d nanoseconds\n" "$c" "$e" >&2
	echo "$e"
}

[ "$#" -ne 0 ] && {
	printf "usage: $0\n"
	exit 1
}

flogos

file1=$base
file2=$mod

p=$(ftime ./diff "$file1" "$file2")
q=$(ftime "diff -u" "$file1" "$file2")

[ "$p" -lt "$q" ] && f="custom diff is faster by $((q - p)) nanoseconds" || f="systems diff is faster by $((p - q)) nanoseconds"

printf "\nin this test %s\n" "$f"

rm -f $base $mod
