use_synth :hollow

# 1. The chord changes every 8 seconds: D, G, B minor, A
chords = (ring chord(:D4, :major), chord(:G4, :major),
          chord(:B3, :minor), chord(:A3, :major))

live_loop :progression do
  set :chord, chords.tick
  sleep 8
end

with_fx :reverb, mix: 0.7 do
  live_loop :flicks do
    use_real_time
    id, strength = sync "/osc*/flick"
    notes = get[:chord] || chord(:D4, :major)
    
    # Remember when each hand last flicked
    set :t1, vt if id == 1
    set :t2, vt if id == 2
    other = (id == 1) ? (get[:t2] || -10) : (get[:t1] || -10)
    
    if vt - other < 0.3
      # 2. Both hands flick together: the whole chord plus a bell
      play notes, attack: 0.1, release: 5, amp: 1.2
      synth :pretty_bell, note: notes[0] + 12, release: 3, amp: 0.5
    elsif id == 1
      # 3. Each hand walks up through the chord: melodies appear
      play notes.tick(:hand1), attack: 0.2, release: 3, amp: strength * 2
    else
      play notes.tick(:hand2) + 12, attack: 0.2, release: 3, amp: strength * 2
    end
  end
end
