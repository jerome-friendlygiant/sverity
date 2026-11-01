// sverity-args: --top top
// expect-none: LNT-057
// The unsized 1 is 32 bits by type, but the sum needs only the counter's width: no truncation is reported.
module top #(parameter W = 4) (input logic clk, output logic [W-1:0] cnt);
  always_ff @(posedge clk) cnt <= cnt + 1;
endmodule
