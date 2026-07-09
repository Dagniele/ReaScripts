-- @noindex
-- chord_dictionary.lua
-- Add new interval mappings here (intervals are calculated as semitones from the lowest note % 12)

-- Smart Dictionary (Modulo 12)
-- 'suffix' is the chord type.
-- 'offset' is the number of semitones to add to the bass note to find the true root.
local chord_dict = {
    -- Two-note intervals (Dyads)
    ["4"] = {suffix = "", offset = 0},            -- Major Dyad (Root + Major 3rd)
    ["3"] = {suffix = "m", offset = 0},           -- Minor Dyad (Root + Minor 3rd)
    
    -- Dyad Inversions (3rd in the bass, Root on top)
    ["8"] = {suffix = "", offset = 8},            -- 1st inv Major Dyad (e.g. E-C -> C/E)
    ["9"] = {suffix = "m", offset = 9},           -- 1st inv Minor Dyad (e.g. C-A -> Am/C)

    -- Root Position Triads
    ["4,7"] = {suffix = "", offset = 0},          -- Major
    ["3,7"] = {suffix = "m", offset = 0},         -- Minor
    ["3,6"] = {suffix = "dim", offset = 0},       -- Diminished
    ["4,8"] = {suffix = "aug", offset = 0},       -- Augmented
    ["5,7"] = {suffix = "sus4", offset = 0},      -- Sus4
    ["2,7"] = {suffix = "sus2", offset = 0},      -- Sus2
    ["7"]   = {suffix = "5", offset = 0},         -- Power chord

    -- Major Inversions
    ["3,8"] = {suffix = "", offset = 8},          -- 1st inv Major (e.g. E-G-C -> C/E)
    ["5,9"] = {suffix = "", offset = 5},          -- 2nd inv Major (e.g. G-C-E -> C/G)

    -- Minor Inversions
    ["4,9"] = {suffix = "m", offset = 9},         -- 1st inv Minor (e.g. Eb-G-C -> Cm/Eb)
    ["5,8"] = {suffix = "m", offset = 5},         -- 2nd inv Minor (e.g. G-C-Eb -> Cm/G)

    -- Diminished Inversions
    ["3,9"] = {suffix = "dim", offset = 9},       -- 1st inv Dim
    ["6,9"] = {suffix = "dim", offset = 6},       -- 2nd inv Dim

    -- Power Chord Inverted
    ["5"]   = {suffix = "5", offset = 5},         -- (e.g. G-C -> C5/G)

    -- Sevenths (Root position)
    ["4,7,11"] = {suffix = "maj7", offset = 0},
    ["3,7,10"] = {suffix = "m7", offset = 0},
    ["4,7,10"] = {suffix = "7", offset = 0},
    ["3,6,9"]  = {suffix = "dim7", offset = 0},
    ["3,6,10"] = {suffix = "m7b5", offset = 0},

    -- Major 7 Inversions
    ["3,7,8"] = {suffix = "maj7", offset = 8},    -- 1st inv
    ["4,5,9"] = {suffix = "maj7", offset = 5},    -- 2nd inv
    ["1,5,8"] = {suffix = "maj7", offset = 1},    -- 3rd inv

    -- Dominant 7 Inversions
    ["3,6,8"] = {suffix = "7", offset = 8},       -- 1st inv
    ["3,5,9"] = {suffix = "7", offset = 5},       -- 2nd inv
    ["2,6,9"] = {suffix = "7", offset = 2},       -- 3rd inv

    -- Minor 7 Inversions
    ["4,7,9"] = {suffix = "m7", offset = 9},      -- 1st inv
    ["3,5,8"] = {suffix = "m7", offset = 5},      -- 2nd inv
    ["2,5,9"] = {suffix = "m7", offset = 2},      -- 3rd inv
    
    -- Extensions
    ["2,4,7"] = {suffix = "add9", offset = 0},
    ["2,3,7"] = {suffix = "m(add9)", offset = 0}
}

return chord_dict