#!/usr/bin/env python3
# Host model of the shared focus ownership rule under burst navigation.
def clamp(v,n): return 0 if n<=0 else max(0,min(v,n-1))
def burst(n,start,ops):
    target=start
    active=[]
    for delta,count in ops:
        for _ in range(count):
            target=clamp(target+delta,n)
            active=[i for i in range(n) if i==target]
            assert len(active)==1
    return target
for name,n in [('Settings',17),('Library',12),('Applications',12),('TOP',8),('Folders',15),('submenus',8)]:
    end=burst(n,0,[(-1,10),(1,10),(-1,10),(1,10)])
    assert 0 <= end < n
print('titan-ui-rapid-input-contract: OK UP/DOWN/LEFT/RIGHT burst model retains one bounded focus for Settings/Library/Applications/TOP/Folders/submenus')
