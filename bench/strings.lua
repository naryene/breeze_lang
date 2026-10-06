local function run(n)
  local a = "hello "
  local b = "world"
  local matches = 0
  local i = 0
  while i < n do
    local s = a .. b
    if s == "hello world" then matches = matches + 1 end
    i = i + 1
  end
  return matches
end
print(run(8000000))
