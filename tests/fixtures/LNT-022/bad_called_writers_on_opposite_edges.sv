// sverity-args: --top top
// The same task writes the register from a posedge and a negedge process: the call is inlined at each site.
// expect: LNT-022 WARNING 8
module top(input logic clk, d, output logic y);
  logic q;
  task put; q <= d; endtask
  always @(posedge clk) put();
  always @(negedge clk) put();
  assign y = q;
endmodule
