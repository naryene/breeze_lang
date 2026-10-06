use std::hint::black_box;

struct Point {
    x: f64,
    y: f64,
}

fn run(n: f64) -> f64 {
    let mut p = Point { x: 0.0, y: 0.0 };
    let mut i = 0.0;
    while i < n {
        p.x = p.x + 1.0;
        p.y = p.y + p.x;
        i = i + 1.0;
    }
    p.y
}

fn main() {
    println!("{}", run(black_box(10000000.0)));
}
