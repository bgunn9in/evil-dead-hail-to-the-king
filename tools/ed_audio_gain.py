"""Bounded oracle for the original PC SFX cached-percent/master calculation."""

def cached_percent(volume: int, master: int = 100) -> int:
    """430372..4303BC; 0.01f is exactly 5368709 / 2**29."""
    volume = max(0, min(100, int(volume)))
    master = int(master) & 255
    return (volume * master * 5368709) // (1 << 29) if master < 100 else volume

def source_gain(volume: int, master: int = 100) -> float:
    """Existing SDL continuous-gain policy, including native cached-percent truncation."""
    return min(cached_percent(volume, master) * (int(master) & 255) * .0001, 10 ** (-458 / 2000))
