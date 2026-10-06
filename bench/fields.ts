class Point {
  x: number = 0;
  y: number = 0;
}

function run(n: number): number {
  const p = new Point();
  p.x = 0;
  p.y = 0;
  let i = 0;
  while (i < n) {
    p.x = p.x + 1;
    p.y = p.y + p.x;
    i = i + 1;
  }
  return p.y;
}
console.log(run(10000000));
