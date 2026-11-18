// Scales a sample by a right shift; gain_sh is a host-written setting.
// Interface:
//   sample  unsigned 8-bit input sample
//   gain_sh right shift applied to the sample
//   scaled  attenuated sample
// Used in the audio front end ahead of the mixer.
//   peak    largest scaled value seen since reset
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [7:0] sample,
  input  logic [2:0] gain_sh,
  output logic [7:0] scaled,
  output logic [7:0] peak
);
  function automatic logic [7:0] apply_gain(input logic [7:0] v);
    return v >> gain_sh;
  endfunction

  always @(sample)
    scaled = apply_gain(sample);

  // peak hold for the level meter
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)              peak <= 8'd0;
    else if (scaled > peak)  peak <= scaled;
  end
endmodule
