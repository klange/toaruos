#!/bin/bash

function cpad {
    word="$1"
    while [ ${#word} -lt $2 ]; do
        word="$word$3";
        if [ ${#word} -lt $2 ]; then
            word="$3$word"
        fi;
    done;
    echo "$word";
}

