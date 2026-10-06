use std::hint::black_box;

fn make_counter() -> impl FnMut() -> f64 {
    let mut count = 0.0;
    move || {
        count = count + 1.0;
        count
    }
}

fn run(n: f64) -> f64 {
    let mut inc = make_counter();
    let mut last = 0.0;
    let mut i = 0.0;
    while i < n {
        last = black_box(inc());
        i = i + 1.0;
    }
    last
}

fn main() {
    println!("{}", run(black_box(15000000.0)));
}
