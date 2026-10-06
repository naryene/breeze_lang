class Point:
    def __init__(self):
        self.x = 0.0
        self.y = 0.0

def run(n):
    p = Point()
    i = 0
    while i < n:
        p.x = p.x + 1
        p.y = p.y + p.x
        i = i + 1
    return p.y

print(run(10000000))
