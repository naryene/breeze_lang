def run(n):
    a = "hello "
    b = "world"
    matches = 0
    i = 0
    while i < n:
        s = a + b
        if s == "hello world":
            matches = matches + 1
        i = i + 1
    return matches

print(run(8000000))
