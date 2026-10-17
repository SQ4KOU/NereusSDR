"""Read genuine AetherSDR derivations from the reconciliation tables."""
import re


def aethersdr_derivative_paths(text: str) -> list[str]:
    """Return ordered, unique source/test paths from Bucket A and C tables.

    The actual Bucket C section ends with an incidental-mentions table before
    the next level-two heading. Only the derivative tables use the first-column
    heading ``NereusSDR file``; the incidental table uses ``File``. Track table
    boundaries as well as bucket headings so those incidental rows stay out.
    """
    bucket = None
    derivative_table = False
    paths = []
    seen = set()
    for raw in text.splitlines():
        line = raw.strip()
        if line.startswith("## "):
            match = re.match(r"## Bucket ([A-D])\b", line)
            bucket = match.group(1) if match else None
            derivative_table = False
            continue
        if bucket not in {"A", "C"} or not line.startswith("|"):
            derivative_table = False
            continue
        cells = [cell.strip() for cell in line.strip("|").split("|")]
        if len(cells) < 2:
            derivative_table = False
            continue
        first = cells[0].strip("`")
        if first == "NereusSDR file":
            derivative_table = True
            continue
        if not derivative_table or not first.startswith(("src/", "tests/")):
            continue
        if first not in seen:
            seen.add(first)
            paths.append(first)
    return paths
