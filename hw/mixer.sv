/*
 * 3-input signed adder with symmetric saturation clamp to int16 range.
 * Combinational. Used inside pvz_audio to mix BGM + 2 SFX voices.
 */
module mixer(
    input  logic signed [15:0] bgm,
    input  logic signed [15:0] sfx0,
    input  logic signed [15:0] sfx1,
    output logic signed [15:0] mixed
);
    logic signed [17:0] sum;  // 3 × 16-bit signed summed safely into 18 bits

    always_comb begin
        sum = $signed({{2{bgm [15]}},  bgm })
            + $signed({{2{sfx0[15]}},  sfx0})
            + $signed({{2{sfx1[15]}},  sfx1});
        if      (sum >  18'sd32767)  mixed = 16'sd32767;
        else if (sum < -18'sd32768)  mixed = -16'sd32768;
        else                         mixed = sum[15:0];
    end
endmodule
