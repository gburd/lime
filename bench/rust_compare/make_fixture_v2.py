#!/usr/bin/env python3
"""Schema-driven JSON fixture generator.

Generates realistic-looking faked JSON content of arbitrary size and
shape.  Five shapes are available, each stresses different aspects of
the lexer:

  api_log    - HTTP API access logs.  Mixed structural + short strings
               + numbers + timestamps.  Closest to JSON Lines workloads.

  product    - E-commerce product catalogue.  Long descriptive strings
               (50-500 chars), nested specs, arrays of variants.
               Closest to schema-driven document stores.

  tweet      - Short social posts with hashtags / mentions.  Many
               small documents, short strings, lots of structural
               punctuation.  Closest to fast-feed indexing.

  metric     - Prometheus-style time-series points.  Heavy on numbers,
               sparse string labels, fixed shape.  Closest to telemetry.

  prose      - Document with long natural-language string bodies
               (1k-10k chars per body).  Stresses string-body fast-path
               specifically -- this is where SIMD scan should win.

Usage:

  python3 make_fixture_v2.py --shape api_log --size-mb 10 --out fixture_v2.json

Multi-document mode (one document per line, Newline-Delimited JSON):

  python3 make_fixture_v2.py --shape product --docs 500 --doc-size-kb 100 \\
                             --out products.ndjson --ndjson

Output is gitignored by convention -- bench/rust_compare/.gitignore
covers fixture*.json and *.ndjson.
"""
import argparse
import json
import random
import string
import sys
import time

# --- value primitives ---------------------------------------------------

LOREM = (
    "lorem ipsum dolor sit amet consectetur adipiscing elit sed do "
    "eiusmod tempor incididunt ut labore et dolore magna aliqua ut "
    "enim ad minim veniam quis nostrud exercitation ullamco laboris "
    "nisi ut aliquip ex ea commodo consequat duis aute irure dolor "
    "in reprehenderit in voluptate velit esse cillum dolore eu fugiat "
    "nulla pariatur excepteur sint occaecat cupidatat non proident "
    "sunt in culpa qui officia deserunt mollit anim id est laborum"
).split()

FIRST_NAMES = ["alice", "bob", "carol", "dave", "eve", "frank", "grace",
               "heidi", "ivan", "judy", "ken", "lara", "mike", "nina",
               "oscar", "penny", "quinn", "ruth", "steve", "trudy"]
LAST_NAMES = ["smith", "jones", "brown", "taylor", "wilson", "davis",
              "miller", "garcia", "rodriguez", "martinez", "hernandez"]
DOMAINS = ["example.com", "test.org", "company.io", "service.net",
           "platform.co", "app.dev"]


def rand_word(rng, lo=3, hi=12):
    n = rng.randint(lo, hi)
    return ''.join(rng.choices(string.ascii_lowercase, k=n))


def rand_sentence(rng, words=10):
    n = rng.randint(words // 2, words * 2)
    return ' '.join(rng.choices(LOREM, k=n))


def rand_paragraph(rng, sentences=8):
    return '. '.join(rand_sentence(rng, words=15) for _ in range(sentences)) + '.'


def rand_email(rng):
    return f"{rng.choice(FIRST_NAMES)}.{rng.choice(LAST_NAMES)}@{rng.choice(DOMAINS)}"


def rand_iso8601(rng):
    y = rng.randint(2020, 2026)
    mo = rng.randint(1, 12)
    d = rng.randint(1, 28)
    h = rng.randint(0, 23)
    mi = rng.randint(0, 59)
    s = rng.randint(0, 59)
    return f"{y:04d}-{mo:02d}-{d:02d}T{h:02d}:{mi:02d}:{s:02d}Z"


def rand_uuid(rng):
    chars = "0123456789abcdef"
    parts = [8, 4, 4, 4, 12]
    return '-'.join(''.join(rng.choices(chars, k=p)) for p in parts)


def rand_ipv4(rng):
    return '.'.join(str(rng.randint(0, 255)) for _ in range(4))


# --- schema generators --------------------------------------------------


def gen_api_log(rng):
    """One HTTP API access log entry."""
    return {
        "timestamp": rand_iso8601(rng),
        "request_id": rand_uuid(rng),
        "method": rng.choice(["GET", "POST", "PUT", "DELETE", "PATCH"]),
        "path": "/" + "/".join(rand_word(rng) for _ in range(rng.randint(1, 4))),
        "status": rng.choice([200, 200, 200, 201, 204, 301, 304, 400, 401, 403, 404, 500]),
        "duration_ms": round(rng.uniform(0.5, 5000.0), 3),
        "user_id": rng.randint(1, 100000) if rng.random() > 0.3 else None,
        "client_ip": rand_ipv4(rng),
        "user_agent": rng.choice([
            "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36",
            "curl/7.81.0",
            "Python-urllib/3.10",
            "Go-http-client/2.0",
            "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) Safari/605.1.15",
        ]),
        "headers": {
            "accept": "application/json",
            "content_type": "application/json" if rng.random() > 0.5 else None,
            "x_forwarded_for": rand_ipv4(rng) if rng.random() > 0.7 else None,
        },
        "bytes_sent": rng.randint(100, 1_000_000),
        "cached": rng.random() > 0.6,
    }


def gen_product(rng):
    """One e-commerce product."""
    n_variants = rng.randint(1, 6)
    return {
        "id": rand_uuid(rng),
        "sku": ''.join(rng.choices(string.ascii_uppercase + string.digits, k=12)),
        "name": ' '.join(rand_word(rng, 4, 10) for _ in range(rng.randint(2, 6))).title(),
        "description": rand_paragraph(rng, sentences=rng.randint(3, 12)),
        "category": rng.choice(["electronics", "clothing", "home", "books", "toys"]),
        "tags": [rand_word(rng) for _ in range(rng.randint(2, 8))],
        "price": {
            "amount": round(rng.uniform(0.99, 9999.99), 2),
            "currency": rng.choice(["USD", "EUR", "GBP", "JPY"]),
        },
        "inventory": {
            "in_stock": rng.randint(0, 1000),
            "reserved": rng.randint(0, 100),
            "warehouse": rand_word(rng, 4, 8).upper(),
        },
        "specs": {
            rand_word(rng): rand_sentence(rng, words=8)
            for _ in range(rng.randint(3, 10))
        },
        "variants": [
            {
                "id": rand_uuid(rng),
                "color": rng.choice(["red", "blue", "green", "black", "white",
                                     "purple", "yellow", "orange"]),
                "size": rng.choice(["XS", "S", "M", "L", "XL", "XXL"]),
                "additional_price": round(rng.uniform(-5.0, 50.0), 2),
            } for _ in range(n_variants)
        ],
        "ratings": {
            "average": round(rng.uniform(1.0, 5.0), 2),
            "count": rng.randint(0, 50000),
            "histogram": {str(i): rng.randint(0, 1000) for i in range(1, 6)},
        },
        "images": [
            f"https://cdn.example.com/products/{rand_uuid(rng)}.jpg"
            for _ in range(rng.randint(1, 5))
        ],
    }


def gen_tweet(rng):
    """One social post."""
    text_words = rng.randint(5, 40)
    text = ' '.join(rng.choices(LOREM, k=text_words))
    if rng.random() > 0.5:
        text += " " + " ".join(f"#{rand_word(rng, 4, 12)}"
                               for _ in range(rng.randint(1, 4)))
    if rng.random() > 0.7:
        text += " " + " ".join(f"@{rand_word(rng, 4, 10)}"
                               for _ in range(rng.randint(1, 3)))
    return {
        "id": rng.randint(10**18, 10**19 - 1),
        "created_at": rand_iso8601(rng),
        "user": {
            "id": rng.randint(1, 1_000_000),
            "screen_name": rand_word(rng, 4, 15),
            "followers": rng.randint(0, 1_000_000),
            "verified": rng.random() > 0.95,
        },
        "text": text,
        "hashtags": [rand_word(rng, 4, 12) for _ in range(rng.randint(0, 5))],
        "mentions": [rand_word(rng, 4, 10) for _ in range(rng.randint(0, 3))],
        "retweet_count": rng.randint(0, 100_000),
        "favorite_count": rng.randint(0, 100_000),
        "lang": rng.choice(["en", "en", "en", "es", "fr", "de", "ja"]),
        "is_retweet": rng.random() > 0.7,
    }


def gen_metric(rng):
    """One Prometheus-style metric point."""
    return {
        "metric": rng.choice([
            "http_requests_total",
            "cpu_usage_percent",
            "memory_bytes",
            "disk_io_seconds",
            "network_bytes_total",
            "process_open_fds",
        ]),
        "labels": {
            "instance": f"server-{rng.randint(1, 100):03d}",
            "region": rng.choice(["us-east-1", "us-west-2", "eu-central-1", "ap-southeast-1"]),
            "service": rand_word(rng, 4, 10),
        },
        "values": [
            [rng.randint(1700000000, 1800000000), round(rng.uniform(0.0, 1e9), 6)]
            for _ in range(rng.randint(50, 200))
        ],
    }


def gen_prose(rng):
    """One document with long natural-language body.  Stresses
    string-body fast-path."""
    return {
        "id": rand_uuid(rng),
        "author": f"{rng.choice(FIRST_NAMES)} {rng.choice(LAST_NAMES)}".title(),
        "title": ' '.join(rng.choices(LOREM, k=rng.randint(3, 8))).title(),
        "published_at": rand_iso8601(rng),
        "abstract": rand_paragraph(rng, sentences=rng.randint(2, 5)),
        "body": '\n\n'.join(rand_paragraph(rng, sentences=rng.randint(8, 15))
                            for _ in range(rng.randint(3, 10))),
        "tags": [rand_word(rng) for _ in range(rng.randint(2, 6))],
        "word_count": rng.randint(500, 5000),
    }


SHAPES = {
    "api_log": gen_api_log,
    "product": gen_product,
    "tweet": gen_tweet,
    "metric": gen_metric,
    "prose": gen_prose,
}


# --- driver -------------------------------------------------------------


def gen_one(shape, rng):
    return SHAPES[shape](rng)


def gen_until_size(shape, rng, target_bytes):
    """Repeatedly generate documents, append to an array, until total
    serialised size meets the target.  Returns the array."""
    docs = []
    cum = 1  # opening '['
    while cum < target_bytes:
        d = gen_one(shape, rng)
        s = json.dumps(d, separators=(',', ':'))
        cum += len(s) + 1  # comma or closing bracket
        docs.append(d)
    return docs


def main():
    ap = argparse.ArgumentParser(
        description="Generate JSON fixtures of arbitrary size and shape.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    ap.add_argument("--shape", choices=sorted(SHAPES.keys()),
                    default="api_log",
                    help="document schema (default: api_log)")
    ap.add_argument("--size-mb", type=float, default=None,
                    help="target output size in MB (single document or array)")
    ap.add_argument("--docs", type=int, default=None,
                    help="number of documents (use with --doc-size-kb for ndjson)")
    ap.add_argument("--doc-size-kb", type=float, default=None,
                    help="target size of each document in KB (use with --docs)")
    ap.add_argument("--seed", type=int, default=42,
                    help="random seed (default: 42)")
    ap.add_argument("--ndjson", action="store_true",
                    help="output one document per line (Newline-Delimited JSON)")
    ap.add_argument("--pretty", action="store_true",
                    help="pretty-print (default: compact)")
    ap.add_argument("--out", default="-",
                    help="output path (default: stdout)")
    args = ap.parse_args()

    rng = random.Random(args.seed)
    indent = 2 if args.pretty else None
    sep = (',', ': ') if args.pretty else (',', ':')

    t0 = time.time()
    if args.docs and args.doc_size_kb:
        # Multi-document mode: N docs, each ~doc_size_kb KB.
        target = int(args.doc_size_kb * 1024)
        out_lines = []
        for i in range(args.docs):
            arr = gen_until_size(args.shape, rng, target)
            out_lines.append(json.dumps(arr, indent=indent, separators=sep))
        text = '\n'.join(out_lines) if args.ndjson else json.dumps(out_lines, indent=indent)
    elif args.size_mb is not None:
        target = int(args.size_mb * 1024 * 1024)
        arr = gen_until_size(args.shape, rng, target)
        text = json.dumps(arr, indent=indent, separators=sep)
    else:
        ap.error("must supply either --size-mb or both --docs and --doc-size-kb")

    elapsed = time.time() - t0
    if args.out == "-":
        sys.stdout.write(text)
    else:
        with open(args.out, "w") as f:
            f.write(text)
    print(f"// shape={args.shape} bytes={len(text):,} ({len(text)/1024/1024:.2f} MB) "
          f"docs={text.count(chr(10)) + 1 if args.ndjson else '?'} "
          f"gen={elapsed:.2f}s",
          file=sys.stderr)


if __name__ == "__main__":
    main()
