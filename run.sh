if [[ -f ./target ]]; then
  rm ./target
fi

gcc src/*.c -o target
./target
