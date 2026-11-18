// sverity-args: --top top
// Two registers on opposite edges of `clk` in one module.
// expect: LNT-085 INFO 6
module top(input logic clk, d, output logic qp, qn);
  always_ff @(posedge clk) qp <= d;
  always_ff @(negedge clk) qn <= d;
endmodule
