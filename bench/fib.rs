use std::hint::black_box;

fn fib(n: f64) -> f64 {
    if n < 2.0 {
        return n;
    }
    fib(n - 1.0) + fib(n - 2.0)
}

fn main() {
    println!("{}", fib(black_box(35.0)));
}
