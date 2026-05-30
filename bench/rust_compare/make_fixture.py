#!/usr/bin/env python3
"""Generate a representative JSON fixture: ~50KB of mixed data."""
import json, random, string
random.seed(42)
def name(): return ''.join(random.choices(string.ascii_lowercase, k=random.randint(3, 10)))
def value(depth=0):
    if depth >= 4 or random.random() < 0.3:
        return random.choice([
            random.randint(-1000, 10000),
            random.uniform(-100.0, 100.0),
            "".join(random.choices(string.ascii_letters + " ", k=random.randint(5, 30))),
            True, False, None,
        ])
    if random.random() < 0.5:
        return [value(depth+1) for _ in range(random.randint(1, 8))]
    return {name(): value(depth+1) for _ in range(random.randint(1, 6))}
data = [value() for _ in range(200)]
text = json.dumps(data)
print(f"// {len(text)} bytes", file=__import__('sys').stderr)
with open('fixture.json', 'w') as f:
    f.write(text)
