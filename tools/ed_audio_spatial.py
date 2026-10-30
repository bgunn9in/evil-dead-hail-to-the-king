"""Read-only oracle for the PC SFX XZ attenuation; no mixer or game-state mutation."""

import math
import struct

from ed_audio_gain import source_gain


def f32(value):
    return struct.unpack('<f',struct.pack('<f',value))[0]


def radius_cm(value: int) -> float:
    return f32(int(value)*f32(.01))


def spatial_percent(volume: int, flags: int, listener, position, listener_radius=20., voice_radius=0.) -> int:
    """430190..43035B before its master scale; finite source float32 coordinates only.

    The float64 oracle retains the unrounded DZ difference where native FST does.
    It does not claim x87 transcendental bit identity at arbitrary rounding ties.
    """
    if len(listener)!=3 or len(position)!=3 or not all(math.isfinite(v) for v in (*listener,*position,listener_radius,voice_radius)):
        raise ValueError('Invalid spatial sound coordinate/radius')
    result=max(0,min(100,int(volume)))
    if flags&4:
        dx=f32(f32(listener[0])-f32(position[0]))
        dz=float(f32(listener[2]))-float(f32(position[2]))
        distance=f32(math.sqrt(dx*dx+dz*f32(dz)))
        radius=f32(voice_radius if flags&8 else listener_radius)
        outside=distance>radius if flags&8 else distance>=radius
        if outside:result=0
        elif radius==0:raise ArithmeticError('Native flag8 zero-radius/zero-distance 0/0 is not normalized')
        else:result=math.trunc((radius-distance)*result/radius)
    return 0 if flags&0x4000 else result


def spatial_gain(volume: int, flags: int, listener, position, listener_radius=20., voice_radius=0., master=100) -> float:
    return source_gain(spatial_percent(volume,flags,listener,position,listener_radius,voice_radius),master)
