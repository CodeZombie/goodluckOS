#!/bin/sh
# Runs on the console (TERMINAL=1 in app.puppy), in this app's folder.
clear
echo "Hello from goodluckOS!"
echo
echo "This app lives in $(pwd)."
# Whatever an app writes goes in data/: it is kept when the app is updated
mkdir -p data
date >> data/runs.txt
echo "It has been opened $(wc -l < data/runs.txt) time(s)."
echo
echo "START + SELECT closes it."
exec sleep 2147483647
