// sverity-args: --top top
// expect: LNT-060 WARNING
module top(input logic clk, output logic y);
  logic q;
  task automatic put; q = 1; endtask
  always @(posedge clk) put();
  always_comb y = q;
endmodule
