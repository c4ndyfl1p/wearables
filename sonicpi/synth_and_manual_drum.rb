# synth_and_manual_drum.rb — persistent synth chord plus a hand-triggered
# hangpan-style melody for sensor 4. A strong down-Z acceleration followed by
# the opposite acceleration plays one note. The player chooses the rhythm.
#
# Requires 4_hand_bridge_calibration.py, which sends
# /motion_xz <sensor_id> <x_accel_m_s2> <z_accel_m_s2>.

DRUM_ID = 4
DOWN_ACCEL_THRESHOLD = 1.5 # m/s^2; CSV shows 20 down/up pairs at this threshold
UP_ACCEL_THRESHOLD = 1.5   # m/s^2; opposite impulse confirms direction changed
HANGPAN_LINE = (ring :d3, :a3, :fs3, :d4, :a3, :e4, :fs3, :a3)

# Persistent synth mockup, matching synth_and_bass.rb and two_hand_music.rb.
set :manual_drum_down_accel_seen, false
with_fx :reverb, mix: 0.6 do
  synth :hollow, note: :d4, sustain: 100000, amp: 0.42, pan: -0.35
  synth :hollow, note: :a4, sustain: 100000, amp: 0.42, pan: 0.35
  synth :pnoise, sustain: 100000, amp: 0.055, cutoff: 60
end

with_fx :reverb, mix: 0.72, room: 0.85 do
  live_loop :manual_hangpan do
    use_real_time
    id, _x_accel, z_accel = sync "/osc*/motion_xz"

    if id == DRUM_ID
      down_accel_seen = get[:manual_drum_down_accel_seen] || false

      if z_accel <= -DOWN_ACCEL_THRESHOLD
        down_accel_seen = true
      elsif down_accel_seen && z_accel >= UP_ACCEL_THRESHOLD
        note = HANGPAN_LINE.tick
        synth :pretty_bell, note: note, amp: 0.62, release: 0.8,
          cutoff: 90, pan: rrand(-0.12, 0.12)
        puts "Sensor #{DRUM_ID} Z reversal: playing #{note}"
        down_accel_seen = false
      end

      set :manual_drum_down_accel_seen, down_accel_seen
    end
  end
end
