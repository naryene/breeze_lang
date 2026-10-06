use std::hint::black_box;

fn run(n: f64) -> f64 {
    let mut sum = 0.0;
    let mut i = 0.0;
    while i < n {
        sum = sum + i;
        i = i + 1.0;
    }
    sum
}

fn main() {
    println!("{}", run(black_box(20000000.0)));
}
