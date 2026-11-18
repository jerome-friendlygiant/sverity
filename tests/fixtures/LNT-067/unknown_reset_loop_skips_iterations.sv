// sverity-args: --top top
// A branch inside the reset loop decides which words are reset: the coverage is undecided, not silent.
// expect: LNT-067 WARNING 6
module top(input logic clk, rst, input logic [1:0] a, input logic [7:0] d, output logic [7:0] y);
  logic [7:0] rf [4];
  always_ff @(posedge clk) if (rst) begin for (int i = 0; i < 4; i++) if (i < 2) rf[i] <= '0; end else rf[a] <= d;
  assign y = rf[a];
endmodule
