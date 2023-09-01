if [[ -f ./target ]]; then
  rm ./target
fi

gcc *.c -o target
./target
