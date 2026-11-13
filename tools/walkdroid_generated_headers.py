"""Find Android-generated include roots in caller-selected output trees."""
from pathlib import Path


def discover(roots):
    found=[]
    for root in roots:
        root=Path(root)
        candidates=[root,*sorted(root.rglob('gen'))]
        for candidate in candidates:
            if (candidate/'android').is_dir() and candidate not in found:
                found.append(candidate)
    return found
