# synth_and_manual_drum.rb — hand-triggered hangpan-style melodies for sensors
# 3 and 4. A strong down-Z acceleration followed by the opposite acceleration
# on either sensor plays one note. Each sensor has its own melody and rhythm.
#
# Requires 4_hand_bridge_calibration.py, which sends
# /motion_xz <sensor_id> <x_accel_m_s2> <z_accel_m_s2>.

DOWN_ACCEL_THRESHOLD = 1.5 # m/s^2; CSV shows 20 down/up pairs at this threshold
UP_ACCEL_THRESHOLD = 1.5   # m/s^2; opposite impulse confirms direction changed
HANGPAN_LINE = (ring :d2, :a2, :d3, :a2, :fs2, :a2, :d3, :a2) 
# (ring :d3, :a3, :fs3, :d4, :a3, :e4, :fs3, :a3)
# HANGPAN_LINE_LOW = (ring :d1, :a1, :d2, :a1, :fs1, :a1, :d2, :a1)
HANGPAN_LINE_LOW = (ring
  :b2, :fs2, :d3, :a2,
  :b2, :fs2, :d3, :fs2
)

set :manual_drum3_down_accel_seen, false
set :manual_drum4_down_accel_seen, false

with_fx :reverb, mix: 0.72, room: 0.85 do
  live_loop :manual_hangpan do
    use_real_time
    id, _x_accel, z_accel = sync "/osc*/motion_xz"

    if id == 3 || id == 4
      if id == 3
        state_key = :manual_drum3_down_accel_seen
        melody = HANGPAN_LINE_LOW
        tick_key = :manual_hangpan_id3
      else
        state_key = :manual_drum4_down_accel_seen
        melody = HANGPAN_LINE
        tick_key = :manual_hangpan_id4
      end

      down_accel_seen = get[state_key] || false

      if z_accel <= -DOWN_ACCEL_THRESHOLD
        down_accel_seen = true
      elsif down_accel_seen && z_accel >= UP_ACCEL_THRESHOLD
        note = melody.tick(tick_key)
        # synth :pretty_bell, note: note, amp: 0.62, release: 0.8,
        #   cutoff: 90, pan: rrand(-0.12, 0.12)
        ###
        # Warm metallic body
        synth :pretty_bell,
          note: note,
          amp: 0.42,
          attack: 0.003,
          release: 1.8,
          cutoff: 110,
          pan: rrand(-0.08, 0.08)

        # Metallic overtones
        synth :fm,
          note: note,
          divisor: 1.4,
          depth: 1.8,
          amp: 0.16,
          attack: 0.002,
          release: 1.3,
          pan: rrand(-0.08, 0.08)

        # Subtle fundamental reinforcement
        synth :sine,
          note: note,
          amp: 0.08,
          attack: 0.005,
          release: 2.0
        ###
        puts "Sensor #{id} Z reversal: playing #{note}"
        down_accel_seen = false
      end

      set state_key, down_accel_seen
    end
  end
end
