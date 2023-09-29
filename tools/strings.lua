infile, outfile = ...
print(infile, outfile)

labels = {}

for line in io.open(infile):lines() do
	local label = line:match("^%s*%[DRIFT_STR_(.*)%]%s*=")
	if label then table.insert(labels, label) end
end

output = io.open(outfile, "w")
for _, label in ipairs(labels)
	do output:write("DRIFT_STR_"..label..",\n")
end
