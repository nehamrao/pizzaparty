

f = open('DESIGNDOC')
line_no = 0
max = 0
for line in f.readlines():
    line_no = line_no + 1
    if len(line) >= max:
        max = len(line)
        print 'at line ' + str(line_no) + '\t' + str(max)


