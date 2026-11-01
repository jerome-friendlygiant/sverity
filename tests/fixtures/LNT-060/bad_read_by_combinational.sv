// sverity-args: --top top
// a combinational process and a continuous assignment read a register that a clocked process writes with a blocking assignment
// expect: LNT-060 WARNING 7
module top(input logic clk, d, output logic y, z);
  logic t;
  always @(posedge clk) t = d;
  assign y = t;
  always_comb z = ~t;
endmodule
