local function run(n)
  local sum = 0.0
  local i = 0.0
  while i < n do
    sum = sum + i
    i = i + 1
  end
  return sum
end
print(run(20000000))
