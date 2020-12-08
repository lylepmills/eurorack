#!/usr/bin/env bash

# Adapted for mac from https://github.com/joeSeggiola/eurorack/blob/stages-multi/stages/makeflash.sh

# This script builds Elements on the vagrant development machine, creating
# the WAV file if desired and plays it.
# Usage:
#   ./makeflash.sh [clean] [size] [wav]

# Build command
COMMAND=""
if [[ $* == *clean* ]]; then
    COMMAND="$COMMAND make -f elements/makefile clean &&"
fi
COMMAND="$COMMAND make -f elements/makefile"
if [[ $* == *size* ]]; then
    COMMAND="$COMMAND && make -f elements/makefile size"
fi
if [[ $* == *wav* ]]; then
    COMMAND="$COMMAND && make -f elements/makefile wav"
fi

# Show command
echo
echo "EXECUTING:"
echo "  $COMMAND"
echo

# Run command on the vagrant development machine
# https://github.com/mklement0/n-install/issues/1#issuecomment-159089258
vagrant ssh -c "set -i; . ~/.bashrc; set +i; $COMMAND"
if [ $? -ne 0 ]; then
    exit $?
fi

# Play the WAV file on the default ALSA device
if [[ $* == *wav* ]]; then
    WAVFILE="`pwd`/../build/elements/elements.wav"
    echo
    echo "PLAYING:"
    echo "  `ls -l "$WAVFILE"`"
    echo
    afplay $WAVFILE
fi

echo

exit 0
