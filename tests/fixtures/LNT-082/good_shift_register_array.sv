// sverity-args: --top top
// An array written only at the indices of a constant-bound loop is a shift register, not a memory: no obligation, no distributed-RAM advice.
// expect-none: LNT-082
module top(input logic clk, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] sr [4];
  always_ff @(posedge clk) begin
    sr[0] <= d;
    for (int i = 1; i < 4; i++) sr[i] <= sr[i-1];
  end
  assign q = sr[3];
endmodule
