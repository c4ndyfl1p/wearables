# two_hand_music.rb — paste into Sonic Pi and press Run (press Stop before running again).
# Needs two_hand_bridge.py running.
#
# Sensor 1: left speaker,  plays D (the root of the chord)
# Sensor 2: right speaker, plays A (the fifth)
# Move both together and you hear the chord.

# Keeps the latest energy of each sensor
live_loop :move_in do
  use_real_time
  id, energy = sync "/osc*/move"
  set :e1, energy if id == 1
  set :e2, energy if id == 2
end

with_fx :reverb, mix: 0.6 do

  # Each sensor has its own endless note and whoosh, starting silent
  n1 = synth :hollow, note: :D4, sustain: 100000, amp: 0, amp_slide: 0.4, pan: -0.6
  n2 = synth :hollow, note: :A4, sustain: 100000, amp: 0, amp_slide: 0.4, pan: 0.6

  w1 = synth :pnoise, sustain: 100000, amp: 0, amp_slide: 0.15,
    cutoff: 60, cutoff_slide: 0.15, pan: -0.6
  w2 = synth :pnoise, sustain: 100000, amp: 0, amp_slide: 0.15,
    cutoff: 60, cutoff_slide: 0.15, pan: 0.6

  # Movement controls each sensor's note and whoosh
  live_loop :follow do
    e1 = get[:e1] || 0
    e2 = get[:e2] || 0
    control n1, amp: e1 * 1.2
    control n2, amp: e2 * 1.2
    control w1, amp: e1 * 0.8, cutoff: 60 + e1 * 60
    control w2, amp: e2 * 0.8, cutoff: 60 + e2 * 60
    sleep 0.05
  end

  # Flicks play higher chord notes on that sensor's side
  live_loop :flicks do
    use_real_time
    id, strength = sync "/osc*/flick"
    if id == 1
      synth :hollow, note: choose([:Fs4, :D5]), release: 2, amp: strength * 1.5, pan: -0.6
    else
      synth :hollow, note: choose([:E5, :Fs5]), release: 2, amp: strength * 1.5, pan: 0.6
    end
  end

end
