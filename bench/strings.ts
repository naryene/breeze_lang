function run(n: number): number {
  const a = "hello ";
  const b = "world";
  let matches = 0;
  let i = 0;
  while (i < n) {
    const s = a + b;
    if (s === "hello world") { matches = matches + 1; }
    i = i + 1;
  }
  return matches;
}
console.log(run(8000000));
