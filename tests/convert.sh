#!/usr/bin/env bash
for in_file in *_in_*.png; do
	# Extract test name from generated artifacts
	test=$(echo "$in_file" | sed -E 's/_in_(.*).png/\1/')
	echo "=> $test"

	# for macOS, `brew install imagemagick`
	magick "_in_${test}.png" "_mid_${test}.png" "_out_${test}.png" +append "../_${test}.png"
done
