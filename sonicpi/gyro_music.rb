# gyro_music.rb — paste into Sonic Pi and press Run.
# Needs gyro_bridge.py running, and in Sonic Pi:
#   Preferences -> IO (or Network) -> "Allow incoming OSC" turned on

use_bpm 110

# Keeps the latest spin speed (degrees per second)
live_loop :gyro_in do
  use_real_time
  data = sync "/osc*/gyro"
  set :speed, data[0].abs
end

# Every full spin plays a sound right away
live_loop :spins do
  use_real_time
  data = sync "/osc*/spin"
  if data[0] > 0
    sample :bd_haus, amp: 2
  else
    sample :sn_dolf, amp: 1.5
  end
end

# Background music that gets busier the faster the stick spins
live_loop :beat do
  speed = get[:speed] || 0

  sample :bd_tek, amp: 1.5 if speed > 150
  sample :drum_cymbal_closed, amp: 0.6 if speed > 400

  if speed > 250
    synth :saw, note: :a2, release: 0.2, amp: 0.5,
      cutoff: 60 + [speed / 20, 60].min    # faster spin = brighter sound
  end

  sleep 0.5
end
