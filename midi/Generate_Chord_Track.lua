-- @description Generate chord track from MIDI item (Smart Dictionary with Inversions & Dyads)
-- @version 1.3
-- @author Open Source Contributor
-- @provides chord_dictionary.lua
-- @about
--   Analyzes a selected MIDI item and generates a dedicated "Chords" track.
--   Uses pitch-class sets (modulo 12) for multi-octave robustness, combined with a 
--   smart dictionary that detects inversions, dyads, and calculates accurate slash chords.

local reaper = reaper

-- 1. Locate the script directory
local _, script_file, _, _ = reaper.get_action_context()
local script_path = script_file:match("(.*[/\\])")

-- 2. Build path for the external dictionary file
local dict_file = script_path .. "chord_dictionary.lua"

-- 3. Load the external dictionary safely
local success, chord_dict = pcall(dofile, dict_file)

if not success or type(chord_dict) ~= "table" then
    reaper.ShowMessageBox("Error: chord_dictionary.lua missing or corrupted.", "File Error", 0)
    return
end

local note_names = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}

-- Function to get or create the "Chords" track
local function get_or_create_chords_track()
    local track_count = reaper.CountTracks(0)
    local chords_track = nil
    
    for i = 0, track_count - 1 do
        local track = reaper.GetTrack(0, i)
        local _, name = reaper.GetTrackName(track)
        if name:lower() == "chords" then
            chords_track = track
            break
        end
    end
    
    if not chords_track then
        reaper.InsertTrackAtIndex(0, true)
        chords_track = reaper.GetTrack(0, 0)
        reaper.GetSetMediaTrackInfo_String(chords_track, "P_NAME", "Chords", true)
    end
    
    return chords_track
end

-- Extract note events from the selected MIDI take
local function get_midi_notes(take)
    local notes = {}
    local _, notecnt = reaper.MIDI_CountEvts(take)
    
    for i = 0, notecnt - 1 do
        local _, selected, muted, startppq, endppq, chan, pitch, vel = reaper.MIDI_GetNote(take, i)
        if not muted then
            local start_time = reaper.MIDI_GetProjTimeFromPPQPos(take, startppq)
            local end_time = reaper.MIDI_GetProjTimeFromPPQPos(take, endppq)
            table.insert(notes, {start = start_time, ["end"] = end_time, pitch = pitch})
        end
    end
    return notes
end

-- Generate a hash of unique time slices
local function get_times_hash(notes)
    local times_hash = {}
    for _, note in ipairs(notes) do
        times_hash[string.format("%.4f", note.start)] = note.start
        times_hash[string.format("%.4f", note["end"])] = note["end"]
    end
    return times_hash
end

-- Identify the chord based on pitch-class sets and inversion mapping
local function identify_chord(active_notes)
    if #active_notes == 0 then return nil end
    if #active_notes == 1 then
        local root_idx = (active_notes[1] % 12) + 1
        return note_names[root_idx]
    end
    
    table.sort(active_notes)
    
    -- The lowest note is the bass
    local bass_note = active_notes[1]
    local bass_pc = bass_note % 12
    local bass_name = note_names[bass_pc + 1]
    
    local intervals_hash = {}
    local intervals = {}
    
    -- Calculate unique pitch classes relative to the bass note
    for i = 2, #active_notes do
        local interval = (active_notes[i] - bass_note) % 12
        if interval > 0 and not intervals_hash[interval] then
            intervals_hash[interval] = true
            table.insert(intervals, interval)
        end
    end
    
    if #intervals == 0 then return bass_name end -- Only octaves of the bass
    
    table.sort(intervals)
    local key = table.concat(intervals, ",")
    
    if chord_dict[key] then
        local offset = chord_dict[key].offset
        local suffix = chord_dict[key].suffix
        
        -- If offset is 0, it's a root position chord
        if offset == 0 then
            return bass_name .. suffix
        else
            -- It's an inversion: calculate the true root and format as a slash chord
            local true_root_pc = (bass_pc + offset) % 12
            local true_root_name = note_names[true_root_pc + 1]
            return true_root_name .. suffix .. "/" .. bass_name
        end
    else
        return "?" -- Unmapped complex voicing
    end
end

-- Check if a given time point falls within an already existing chord item
local function is_in_existing_item(mid_point, overlapping_items)
    for _, c_item in ipairs(overlapping_items) do
        local c_start = reaper.GetMediaItemInfo_Value(c_item, "D_POSITION")
        local c_end = c_start + reaper.GetMediaItemInfo_Value(c_item, "D_LENGTH")
        if mid_point > c_start and mid_point < c_end then
            return true
        end
    end
    return false
end

-- Main execution function
local function main()
    local item = reaper.GetSelectedMediaItem(0, 0)
    if not item then
        reaper.ShowMessageBox("Please select a MIDI item before running the script.", "Missing Selection", 0)
        return
    end
    
    local take = reaper.GetActiveTake(item)
    if not take or not reaper.TakeIsMIDI(take) then
        reaper.ShowMessageBox("The selected item is not a valid MIDI item.", "Invalid Item", 0)
        return
    end
    
    local midi_start = reaper.GetMediaItemInfo_Value(item, "D_POSITION")
    local midi_end = midi_start + reaper.GetMediaItemInfo_Value(item, "D_LENGTH")
    
    local chords_track = get_or_create_chords_track()
    
    local overlapping_items = {}
    local item_count = reaper.CountTrackMediaItems(chords_track)
    for i = 0, item_count - 1 do
        local c_item = reaper.GetTrackMediaItem(chords_track, i)
        local c_start = reaper.GetMediaItemInfo_Value(c_item, "D_POSITION")
        local c_end = c_start + reaper.GetMediaItemInfo_Value(c_item, "D_LENGTH")
        
        if c_end > midi_start and c_start < midi_end then
            table.insert(overlapping_items, c_item)
        end
    end
    
    local write_mode = "overwrite"
    
    if #overlapping_items > 0 then
        local msg = "Existing chords found in this area.\n\nYES: Overwrite existing chords\nNO: Keep existing and only fill empty spaces\nCANCEL: Abort"
        local response = reaper.ShowMessageBox(msg, "Overwrite Warning", 3)
        
        if response == 2 then return end 
        if response == 6 then write_mode = "overwrite" end 
        if response == 7 then write_mode = "fill" end 
    end
    
    reaper.Undo_BeginBlock()
    
    if write_mode == "overwrite" then
        for i = #overlapping_items, 1, -1 do
            reaper.DeleteTrackMediaItem(chords_track, overlapping_items[i])
        end
    end
    
    local notes = get_midi_notes(take)
    local times_hash = get_times_hash(notes)
    
    if write_mode == "fill" then
        for _, c_item in ipairs(overlapping_items) do
            local c_start = reaper.GetMediaItemInfo_Value(c_item, "D_POSITION")
            local c_end = c_start + reaper.GetMediaItemInfo_Value(c_item, "D_LENGTH")
            times_hash[string.format("%.4f", c_start)] = c_start
            times_hash[string.format("%.4f", c_end)] = c_end
        end
    end
    
    times_hash[string.format("%.4f", midi_start)] = midi_start
    times_hash[string.format("%.4f", midi_end)] = midi_end
    
    local slices = {}
    for _, t in pairs(times_hash) do 
        if t >= midi_start and t <= midi_end then
            table.insert(slices, t) 
        end
    end
    table.sort(slices)
    
    local current_chord = nil
    local current_start = nil
    
    local function flush_chord(end_time)
        if current_chord then
            if end_time - current_start > 0.005 then
                local empty_item = reaper.AddMediaItemToTrack(chords_track)
                reaper.SetMediaItemInfo_Value(empty_item, "D_POSITION", current_start)
                reaper.SetMediaItemInfo_Value(empty_item, "D_LENGTH", end_time - current_start)
                reaper.GetSetMediaItemInfo_String(empty_item, "P_NOTES", current_chord, true)
            end
        end
    end
    
    for i = 1, #slices - 1 do
        local slice_start = slices[i]
        local slice_end = slices[i+1]
        local mid_point = slice_start + ((slice_end - slice_start) / 2)
        
        local skip_slice = false
        if write_mode == "fill" and is_in_existing_item(mid_point, overlapping_items) then
            skip_slice = true
        end
        
        if skip_slice then
            flush_chord(slice_start)
            current_chord = nil
            current_start = nil
        else
            local active_pitches = {}
            for _, note in ipairs(notes) do
                if note.start <= mid_point and note["end"] >= mid_point then
                    table.insert(active_pitches, note.pitch)
                end
            end
            
            local chord_name = identify_chord(active_pitches)
            
            if chord_name ~= current_chord then
                flush_chord(slice_start)
                current_chord = chord_name
                current_start = slice_start
            end
        end
    end
    
    flush_chord(slices[#slices])
    
    reaper.UpdateArrange()
    reaper.Undo_EndBlock("Generate Chord Track", -1)
end

main()
