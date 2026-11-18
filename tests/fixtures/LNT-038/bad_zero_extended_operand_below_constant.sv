// sverity-args: --top top
// The leading constant zero keeps the value at most 7, so the test against 8 is true for every input.
// expect: LNT-038 WARNING 7
// expect: LNT-038 WARNING 8
// expect-count: LNT-038 WARNING 2
module top(input logic [2:0] sel, input logic [7:0] data, output logic accepted, output logic masked_big);
  assign accepted = ({1'b0, sel} < 4'd8);
  assign masked_big = ((data & 8'h0f) > 8'd15);   // a four-bit mask is never above 15
endmodule
