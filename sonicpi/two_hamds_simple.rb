use_synth :hollow

with_fx :reverb, mix: 0.7 do
  live_loop :flicks do
    use_real_time
    id, strength = sync "/osc*/flick"
    if id == 1
      play choose([:D4, :E4]), attack: 0.5, release: 4
    else
      play choose([:Fs4, :A4]), attack: 0.5, release: 4
    end
  end
end
