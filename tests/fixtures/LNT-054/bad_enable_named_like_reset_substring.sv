// sverity-args: --top top
// `first_beat` holds the letters "rst" but is not a reset; the counter has no reset value.
// expect: LNT-054 WARNING 8
module top(input logic clk, first_beat, valid, output logic y);
  logic [3:0] beat;
  always_ff @(posedge clk) if (first_beat) beat <= '0; else if (valid) beat <= beat + 4'd1;
  always_comb begin
    if (beat == 4'd3) y = 1'b1; else y = 1'b0;
  end
endmodule
