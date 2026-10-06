function makeCounter(): () => number {
  let count = 0;
  return function inc(): number {
    count = count + 1;
    return count;
  };
}

function run(n: number): number {
  const inc = makeCounter();
  let last = 0;
  let i = 0;
  while (i < n) {
    last = inc();
    i = i + 1;
  }
  return last;
}
console.log(run(15000000));
