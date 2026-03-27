#!/bin/bash

run_osu() {
  local HOSTS=$1        # mpirun --host

  if [ -z "$HOSTS" ]; then
    echo "run_osu <hosts>"
    return 1
  fi

  # Count hosts = number of commas + 1
  local NUM_HOSTS=$(( $(grep -o "," <<< "$HOSTS" | wc -l) + 1 ))

  # Filenames
  local OUT_LOG="output_hosts${NUM_HOSTS}_iter${ITER}_size${MSG_SIZE}.log"

  echo "Running on $NUM_HOSTS hosts"
  echo "Log output  -> $OUT_LOG"

  # Run mpirun and redirect stdout/stderr to .log
  mpirun \
    --host "$HOSTS" \
    --prefix /home/di75gix/mpitofino/projects/openmpi/install/ \
    --mca pml ucx \
    --mca coll mtof,basic,libnbc \
    -x LD_LIBRARY_PATH=/home/di75gix/mpitofino/projects/mpitofino/build/client_lib/ \
    --tag-output \
    ./install/libexec/osu-micro-benchmarks/mpi/collective/osu_allreduce \
    > "$OUT_LOG" 2>&1
}

# --- parameter spaces ---
HOST_LISTS=(
#  "wolpy12"
  "wolpy12:2,wolpy13:2"
  "wolpy12:2,wolpy13:2,wolpy14:2"
  "wolpy12:2,wolpy13:2,wolpy14:2,wolpy15:2"
  "wolpy12:2,wolpy13:2,wolpy14:2,wolpy15:2,wolpy16:2"
)
for HOSTS in "${HOST_LISTS[@]}"; do
    echo "OSU run: hosts=$HOSTS"
    run_osu "$HOSTS"
done
