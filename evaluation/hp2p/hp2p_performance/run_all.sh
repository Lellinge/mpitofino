#!/bin/bash

run_hp2p() {
  local HOSTS=$1        # mpirun --host
  local ITER=$2         # -n (iterations)
  local MSG_SIZE=$3     # -s (message size)

  if [ -z "$HOSTS" ] || [ -z "$ITER" ] || [ -z "$MSG_SIZE" ]; then
    echo "run_hp2p <hosts> <iterations> <message_size>"
    return 1
  fi

  # Count hosts = number of commas + 1
  local NUM_HOSTS=$(( $(grep -o "," <<< "$HOSTS" | wc -l) + 1 ))

  # Filenames
  local OUT_HTML="output_hosts${NUM_HOSTS}_iter${ITER}_size${MSG_SIZE}.html"
  local OUT_LOG="output_hosts${NUM_HOSTS}_iter${ITER}_size${MSG_SIZE}.log"

  echo "Running on $NUM_HOSTS hosts, iterations=$ITER, size=$MSG_SIZE"
  echo "HTML output -> $OUT_HTML"
  echo "Log output  -> $OUT_LOG"

  # Run mpirun and redirect stdout/stderr to .log
  mpirun \
    --host "$HOSTS" \
    --prefix /home/di75gix/mpitofino/projects/openmpi/install/ \
    --mca pml ucx \
    --mca coll mtof,basic,libnbc \
    -x LD_LIBRARY_PATH=/home/di75gix/mpitofino/projects/mpitofino/build/client_lib/ \
    --tag-output \
    ./install/bin/hp2p.exe \
    -n "$ITER" \
    -s "$MSG_SIZE" \
    -o first_test \
    -o "$OUT_HTML" \
    > "$OUT_LOG" 2>&1
}


# --- parameter spaces ---
HOST_LISTS=(
#  "wolpy12"
  "wolpy12:4,wolpy13:4"
  "wolpy12:4,wolpy13:4,wolpy14:4"
  "wolpy12:4,wolpy13:4,wolpy14:4,wolpy15:4"
  "wolpy12:4,wolpy13:4,wolpy14:4,wolpy15:4,wolpy16:4"
)

# OSU-like message sizes (bytes)
MSG_SIZES=(
  1 2 4 8 16 32 64 128 256 512
  1024 2048 4096 8192 16384 32768 65536
  131072 262144 524288
  1048576 2097152 4194304
)

# Function to choose iterations OSU-style
osu_iterations() {
  local size=$1

  if (( size <= 1024 )); then
    echo 20000
  elif (( size <= 65536 )); then
    echo 5000
  elif (( size <= 1048576 )); then
    echo 1000
  else
    echo 100
  fi
}


# --- nested loops ---
for HOSTS in "${HOST_LISTS[@]}"; do
  for SIZE in "${MSG_SIZES[@]}"; do
    ITER=$(osu_iterations "$SIZE")
    echo "OSU-style run: hosts=$HOSTS size=$SIZE iter=$ITER"
    run_hp2p "$HOSTS" "$ITER" "$SIZE"
  done
done

