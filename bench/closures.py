def make_counter():
    count = 0
    def inc():
        nonlocal count
        count = count + 1
        return count
    return inc

def run(n):
    inc = make_counter()
    last = 0
    i = 0
    while i < n:
        last = inc()
        i = i + 1
    return last

print(run(15000000))
