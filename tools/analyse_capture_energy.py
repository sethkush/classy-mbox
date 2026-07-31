import math
def dbfs(p): return 10*math.log10(p)

print("Measured, from FINDING_capture_8frame_artifact.md (all four runs):")
print("  ch1/ch2 broadband RMS = -3.5 to -3.6 dBFS, identical at 44.1 and 48 kHz,")
print("  identical whether or not anything was playing.")
print("  Tone bins @1000/1500 Hz = -87 to -96 dBFS (the numerical floor).")
print()
print("What a working ADC on a quiet input gives: roughly -80 to -95 dBFS RMS.")
print("The measurement is ~90 dB hotter than that. So test the noise hypothesis:")
print()

# 3/8 of samples are pinned to +/- full scale (the measured rail pattern).
# Model the other 5/8 as uniform noise of half-amplitude 'a' (a=1.0 => full scale).
for a in (1.0, 0.75, 0.5, 0.4, 0.35, 0.25):
    p = (5/8)*(a*a/3) + (3/8)*1.0      # E[x^2]; uniform(-a,a) has E=a^2/3
    print(f"  good samples uniform +/-{a:<5}  ->  RMS = {dbfs(p):6.2f} dBFS")
print()
p_rails_only = (3/8)*1.0
print(f"  rails alone, good samples silent -> RMS = {dbfs(p_rails_only):6.2f} dBFS  (floor set by the rails)")
print()
print("So even if the 5/8 'good' samples were digital silence, the 3/8 rails alone")
print(f"put the floor at {dbfs(p_rails_only):.2f} dBFS. The measurement sits at -3.5 dBFS,")
print("which needs the good samples to carry real energy too -- about half scale.")
print()
print("Reported 'good' sample values: 2772782, -2706029, 2121805, 177505, 519251")
print(f"  full scale = 8388607; so those are {2772782/8388607:.2f}, {-2706029/8388607:.2f}, "
      f"{2121805/8388607:.2f}, {177505/8388607:.2f}, {519251/8388607:.2f} of FS")
vals=[2772782,-2706029,2121805,177505,519251]
rms=math.sqrt(sum(v*v for v in vals)/len(vals))/8388607
print(f"  RMS of that sample of 5 = {rms:.3f} FS = {20*math.log10(rms):.1f} dBFS")
