/* Test fixture offsets for tb_voice_sfx. 3 cues, indices 1..3; 4..8 unused. */
parameter logic [14:0] SFX_START [1:8] = '{
    15'd0, 15'd4, 15'd8, 15'd0, 15'd0, 15'd0, 15'd0, 15'd0
};
parameter logic [14:0] SFX_END   [1:8] = '{
    15'd3, 15'd7, 15'd11, 15'd0, 15'd0, 15'd0, 15'd0, 15'd0
};
