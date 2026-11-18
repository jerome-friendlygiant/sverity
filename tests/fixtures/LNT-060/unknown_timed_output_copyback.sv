// sverity-args: --top top
// expect-unresolved: LNT-060
module top(input logic clk, output logic y);
  logic q;
  task automatic put(output logic v); #1 v = 1; endtask
  always @(posedge clk) put(q);
  assign y = q;
endmodule
