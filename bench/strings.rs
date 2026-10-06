use std::hint::black_box;

fn run(n: f64) -> f64 {
    let a = black_box(String::from("hello "));
    let b = black_box(String::from("world"));
    let mut matches = 0.0;
    let mut i = 0.0;
    while i < n {
        let s = black_box(a.clone() + &b);
        if s == "hello world" {
            matches = matches + 1.0;
        }
        i = i + 1.0;
    }
    matches
}

fn main() {
    println!("{}", run(black_box(8000000.0)));
}
