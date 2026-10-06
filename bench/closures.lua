local function make_counter()
  local count = 0
  return function()
    count = count + 1
    return count
  end
end

local function run(n)
  local inc = make_counter()
  local last = 0
  local i = 0
  while i < n do
    last = inc()
    i = i + 1
  end
  return last
end
print(run(15000000))
