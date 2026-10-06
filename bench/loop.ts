function run(n: number): number {
  let sum = 0;
  let i = 0;
  while (i < n) {
    sum = sum + i;
    i = i + 1;
  }
  return sum;
}
console.log(run(20000000));
