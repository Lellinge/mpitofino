#!/bin/bash -e

$SDE/run_bfshell.sh -f <( cat << "EOF"
ucli
bf_pltfm
qsfp
show
..
..
exit
exit
EOF
)
