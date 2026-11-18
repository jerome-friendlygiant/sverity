// Blocking write first: the read after it in the same process sees the new word in simulation; whether the target primitive does is not established.
// expect: LNT-082 WARNING
module top(input logic clk, input logic [1:0] a, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] mem [4];
  always_ff @(posedge clk) begin
    mem[a] = d;
    q <= mem[a];
  end
endmodule
