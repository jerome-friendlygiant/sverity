// sverity-args: --top top
// A hierarchical read of a memory the child writes blocking-first: the obligation reaches across the boundary.
// expect: LNT-082 WARNING
module child(input logic clk, input logic [1:0] a, input logic [7:0] d);
  logic [7:0] mem[4];
  always_ff @(posedge clk) mem[a] = d;
endmodule
module top(input logic clk, input logic [1:0] a, input logic [7:0] d, output logic [7:0] q);
  child u(clk,a,d);
  assign q=u.mem[a];
endmodule
