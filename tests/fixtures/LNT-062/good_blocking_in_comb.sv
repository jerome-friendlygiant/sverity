// sverity-args: --top top
// blocking in combinational logic, nonblocking in a clocked block and in always_latch: no finding
// expect-none: LNT-062
module top(input logic clk, a, b, en, output logic y, q, l);
  always_comb y = a & b;
  always_ff @(posedge clk) q <= a;
  always_latch if (en) l <= b;
endmodule
