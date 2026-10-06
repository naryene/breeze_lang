local function run(n)
  local p = { x = 0.0, y = 0.0 }
  local i = 0
  while i < n do
    p.x = p.x + 1
    p.y = p.y + p.x
    i = i + 1
  end
  return p.y
end
print(run(10000000))
